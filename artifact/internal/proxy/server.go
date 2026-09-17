// Package proxy implements the FeCaD Proxy service
// the trusted intermediary between the user and the providers: it holds the
// KSKs that switch provider results into the user's key, fans queries out in
// parallel, and applies the cancellation CTs of consenting patients before
// key-switching
package proxy

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"go.uber.org/zap"

	"github.com/fecad/internal/blssig"
	"github.com/fecad/internal/crypto"
	"github.com/fecad/internal/metrics"
	"github.com/fecad/internal/patient"
)

// server parameters
type Config struct {
	DataDir       string
	ListenAddr    string
	Backend       crypto.Backend
	Log           *zap.Logger
	BenchmarkMode bool // skip consent window; all registered patients auto-consent immediately
}

// one registered provider
type provider struct {
	ID          string `json:"id"`
	Address     string `json:"address"` // http://host:port
	SKDir       string `json:"sk_dir"`  // directory with the provider's secret_key.bin
	KSKPath     string // ksk_{L→R}: aggregate (result) key switch
	KSKRtoLPath string // ksk_{R→L}: encrypted-literal key switch (paper §3.4)
	// slot offset in the federation-wide indicator vector; ksk_{L→R} carries
	// the matching rotation
	RowOffset int
}

// one patient's consent registration
type patientRecord struct {
	PatientID         string
	PKHex             string
	CancellationCTHex string // Enc(−r_p) bytes, stored in DataDir/cancellations/{id}.bin
	RecordIDs         []int  // row indices owned by this patient (used for row_to_patient.json)
}

// in-flight consent for one qd
type pendingQD struct {
	QD           string
	Instructions string
	CreatedAt    time.Time
	Sigs         map[string]string // patient_id -> sig_hex
	Closed       bool              // bench finished pushing envelopes
}

type Server struct {
	cfg       Config
	m         *metrics.Writer
	r         *chi.Mux
	mu        sync.Mutex
	providers map[string]*provider
	// consent state
	patients map[string]*patientRecord // patient_id -> record
	pending  map[string]*pendingQD     // qd -> pending
	// prePending and preClose cover the race between the patient-envelope push
	// and /query-request registering the qd: both are drained when the
	// pendingQD is created, so early signatures and closes are not lost
	prePending map[string]map[string]string // qd -> patient_id -> sig_hex
	preClose   map[string]bool              // qd -> close-arrived-early flag
	// registration POSTs are concurrent, so the read-modify-write of
	// row_to_patient.json is serialised here; rowToPatient mirrors the file
	// in memory so registration does not re-read it from BeeGFS per patient
	rowToPatientMu     sync.Mutex
	rowToPatient       []string
	rowToPatientLoaded bool
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0o755); err != nil {
		cfg.Log.Warn("could not create data dir", zap.Error(err))
	}
	os.MkdirAll(filepath.Join(cfg.DataDir, "cancellations"), 0o755)
	s := &Server{
		cfg:        cfg,
		m:          metrics.NewWriter(cfg.DataDir),
		providers:  make(map[string]*provider),
		patients:   make(map[string]*patientRecord),
		pending:    make(map[string]*pendingQD),
		prePending: make(map[string]map[string]string),
		preClose:   make(map[string]bool),
	}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Post("/register", s.handleRegister)
	s.r.Post("/install-ksk", s.handleInstallKSK)
	s.r.Post("/query-request", s.handleQueryRequest)
	s.r.Post("/consent/register-patient", s.handleRegisterPatient)
	s.r.Post("/consent/register-batch", s.handleRegisterBatch)
	s.r.Post("/consent/sign-batch", s.handleConsentSignBatch)
	s.r.Get("/consent/pending", s.handleConsentPending)
	s.r.Post("/consent/sign", s.handleConsentSign)
	s.r.Post("/consent/close", s.handleConsentClose)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.WriteHeader(http.StatusOK)
}

// Provider registration

// POST /register body
type RegisterRequest struct {
	ProviderID   string `json:"provider_id"`
	Address      string `json:"address"`
	SKDir        string `json:"sk_dir"`
	ResearcherSK string `json:"researcher_sk_dir"` // researcher's SK directory
}

// register a provider, pre-generating its KSK
func (s *Server) handleRegister(w http.ResponseWriter, r *http.Request) {
	var req RegisterRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	kskPath := filepath.Join(s.cfg.DataDir, fmt.Sprintf("ksk_%s.bin", req.ProviderID))

	tKSK := time.Now()
	if err := s.cfg.Backend.GenerateKSK(req.ResearcherSK, req.SKDir, kskPath); err != nil {
		s.cfg.Log.Error("GenerateKSK failed", zap.String("entity", "proxy"), zap.String("provider", req.ProviderID), zap.Error(err))
		http.Error(w, fmt.Sprintf("KSK generation failed: %v", err), http.StatusInternalServerError)
		return
	}
	s.m.Record("proxy-generate-ksk", time.Since(tKSK), metrics.Meta{Status: req.ProviderID}, 0, 0)

	s.mu.Lock()
	s.providers[req.ProviderID] = &provider{
		ID:      req.ProviderID,
		Address: req.Address,
		SKDir:   req.SKDir,
		KSKPath: kskPath,
	}
	s.mu.Unlock()

	s.cfg.Log.Info("provider registered", zap.String("entity", "proxy"), zap.String("id", req.ProviderID), zap.String("addr", req.Address))
	w.WriteHeader(http.StatusOK)
}

// POST /install-ksk body
type InstallKSKRequest struct {
	RunID           string `json:"run_id"`
	KMURL           string `json:"km_url"`
	ProviderID      string `json:"provider_id"`
	RowOffset       int    `json:"row_offset"`
	ProviderIndex   int    `json:"provider_index"`
	ProviderAddress string `json:"provider_address"`
	ProviderSKDir   string `json:"provider_sk_dir"` // recorded for diagnostics only
}

// fetch both KSKs from the KM and register the provider
// replaces handleRegister when a Key Manager is in use, so the proxy never
// sees the underlying SKs (paper §3.3)
func (s *Server) handleInstallKSK(w http.ResponseWriter, r *http.Request) {
	var req InstallKSKRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.KMURL == "" || req.ProviderID == "" {
		http.Error(w, "km_url and provider_id required", http.StatusBadRequest)
		return
	}

	kskLR := filepath.Join(s.cfg.DataDir, fmt.Sprintf("ksk_%s.bin", req.ProviderID))
	kskRL := filepath.Join(s.cfg.DataDir, fmt.Sprintf("ksk_rl_%s.bin", req.ProviderID))

	meta := metrics.ParseRunID(req.RunID)
	meta.Status = req.ProviderID

	for _, dl := range []struct {
		direction string
		dst       string
		phase     string
		mandatory bool
	}{
		{"LtoR", kskLR, "proxy-install-ksk-LtoR", true},
		{"RtoL", kskRL, "proxy-install-ksk-RtoL", false},
	} {
		t := time.Now()
		url := fmt.Sprintf("%s/ksk?provider_id=%d&direction=%s",
			req.KMURL, req.ProviderIndex, dl.direction)
		resp, err := http.Get(url)
		if err != nil {
			if dl.mandatory {
				http.Error(w, "fetch KSK: "+err.Error(), http.StatusBadGateway)
				return
			}
			s.cfg.Log.Warn("optional KSK fetch failed",
				zap.String("entity", "proxy"),
				zap.String("direction", dl.direction), zap.Error(err))
			continue
		}
		if resp.StatusCode != http.StatusOK {
			resp.Body.Close()
			if dl.mandatory {
				http.Error(w, fmt.Sprintf("KM status %d", resp.StatusCode),
					http.StatusBadGateway)
				return
			}
			s.cfg.Log.Warn("optional KSK not available at KM",
				zap.String("entity", "proxy"),
				zap.String("direction", dl.direction),
				zap.Int("status", resp.StatusCode))
			continue
		}
		data, err := io.ReadAll(resp.Body)
		resp.Body.Close()
		if err != nil {
			if dl.mandatory {
				http.Error(w, "read KSK: "+err.Error(), http.StatusInternalServerError)
				return
			}
			continue
		}
		if err := os.WriteFile(dl.dst, data, 0o600); err != nil {
			http.Error(w, "write KSK: "+err.Error(), http.StatusInternalServerError)
			return
		}
		s.m.Record(dl.phase, time.Since(t), meta, 0, int64(len(data)))
	}

	s.mu.Lock()
	s.providers[req.ProviderID] = &provider{
		ID:          req.ProviderID,
		Address:     req.ProviderAddress,
		SKDir:       req.ProviderSKDir,
		KSKPath:     kskLR,
		KSKRtoLPath: kskRL,
		RowOffset:   req.RowOffset,
	}
	s.mu.Unlock()

	s.cfg.Log.Info("KSKs installed and provider registered",
		zap.String("entity", "proxy"),
		zap.String("id", req.ProviderID),
		zap.String("addr", req.ProviderAddress))
	w.WriteHeader(http.StatusOK)
}

// Consent endpoints

// store a patient's pk and cancellation CT
// also extends row_to_patient.json, which the he3db backend reads
func (s *Server) handleRegisterPatient(w http.ResponseWriter, r *http.Request) {
	var req struct {
		PatientID         string `json:"patient_id"`
		PKHex             string `json:"pk_hex"`
		CancellationCTHex string `json:"cancellation_ct_hex"`
		RecordIDs         []int  `json:"record_ids"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	cancelDir := filepath.Join(s.cfg.DataDir, "cancellations")

	if req.CancellationCTHex != "" {
		ctData, err := hex.DecodeString(req.CancellationCTHex)
		if err != nil {
			http.Error(w, "invalid cancellation_ct_hex", http.StatusBadRequest)
			return
		}
		ctPath := filepath.Join(cancelDir, req.PatientID+".bin")
		if err := os.WriteFile(ctPath, ctData, 0o644); err != nil {
			http.Error(w, "write cancellation CT: "+err.Error(), http.StatusInternalServerError)
			return
		}
	}

	if len(req.RecordIDs) > 0 {
		if err := s.updateRowToPatient(cancelDir, req.PatientID, req.RecordIDs); err != nil {
			s.cfg.Log.Warn("updateRowToPatient failed", zap.String("entity", "proxy"), zap.Error(err))
		}
	}

	s.mu.Lock()
	s.patients[req.PatientID] = &patientRecord{
		PatientID:         req.PatientID,
		PKHex:             req.PKHex,
		CancellationCTHex: req.CancellationCTHex,
		RecordIDs:         req.RecordIDs,
	}
	s.mu.Unlock()

	w.WriteHeader(http.StatusOK)
}

// register every patient of one provider at once
// saves n round-trips and n rewrites of row_to_patient.json; the rewrites are
// quadratic on a shared filesystem and were the enrollment bottleneck at 10k
func (s *Server) handleRegisterBatch(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Patients []struct {
			PatientID         string `json:"patient_id"`
			PKHex             string `json:"pk_hex"`
			CancellationCTHex string `json:"cancellation_ct_hex"`
			RecordIDs         []int  `json:"record_ids"`
		} `json:"patients"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	cancelDir := filepath.Join(s.cfg.DataDir, "cancellations")

	rows := make(map[string][]int, len(req.Patients))
	for _, p := range req.Patients {
		if p.CancellationCTHex != "" {
			ctData, err := hex.DecodeString(p.CancellationCTHex)
			if err != nil {
				http.Error(w, "invalid cancellation_ct_hex for "+p.PatientID, http.StatusBadRequest)
				return
			}
			if err := os.WriteFile(filepath.Join(cancelDir, p.PatientID+".bin"), ctData, 0o644); err != nil {
				http.Error(w, "write cancellation CT: "+err.Error(), http.StatusInternalServerError)
				return
			}
		}
		if len(p.RecordIDs) > 0 {
			rows[p.PatientID] = p.RecordIDs
		}
	}
	if len(rows) > 0 {
		if err := s.updateRowToPatientBatch(cancelDir, rows); err != nil {
			s.cfg.Log.Warn("updateRowToPatientBatch failed", zap.String("entity", "proxy"), zap.Error(err))
		}
	}

	s.mu.Lock()
	for _, p := range req.Patients {
		s.patients[p.PatientID] = &patientRecord{
			PatientID:         p.PatientID,
			PKHex:             p.PKHex,
			CancellationCTHex: p.CancellationCTHex,
			RecordIDs:         p.RecordIDs,
		}
	}
	s.mu.Unlock()

	s.cfg.Log.Info("patients registered", zap.String("entity", "proxy"), zap.Int("n", len(req.Patients)))
	w.WriteHeader(http.StatusOK)
}

// record many envelope signatures at once
// verification is deferred to aggregateAndVerifyConsent (paper §4.3), since
// checking here as well would charge the pairing cost twice
func (s *Server) handleConsentSignBatch(w http.ResponseWriter, r *http.Request) {
	var req struct {
		QD         string `json:"qd"`
		Signatures []struct {
			PatientID string `json:"patient_id"`
			SigHex    string `json:"sig_hex"`
		} `json:"signatures"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	s.mu.Lock()
	pq, ok := s.pending[req.QD]
	var buf map[string]string
	if !ok {
		buf = s.prePending[req.QD]
		if buf == nil {
			buf = make(map[string]string, len(req.Signatures))
			s.prePending[req.QD] = buf
		}
	}
	for _, sig := range req.Signatures {
		if _, known := s.patients[sig.PatientID]; !known {
			continue
		}
		if ok {
			pq.Sigs[sig.PatientID] = sig.SigHex
		} else {
			buf[sig.PatientID] = sig.SigHex
		}
	}
	s.mu.Unlock()

	s.cfg.Log.Info("consent batch received", zap.String("entity", "patient"),
		zap.Int("n", len(req.Signatures)), zap.String("qd", req.QD[:8]+"…"))
	w.WriteHeader(http.StatusOK)
}

// updateRowToPatient for many patients
func (s *Server) updateRowToPatientBatch(cancelDir string, rows map[string][]int) error {
	s.rowToPatientMu.Lock()
	defer s.rowToPatientMu.Unlock()
	path := filepath.Join(cancelDir, "row_to_patient.json")
	if !s.rowToPatientLoaded {
		if raw, err := os.ReadFile(path); err == nil {
			json.Unmarshal(raw, &s.rowToPatient) //nolint:errcheck
		}
		s.rowToPatientLoaded = true
	}
	maxRow := 0
	for _, idx := range rows {
		for _, r := range idx {
			if r > maxRow {
				maxRow = r
			}
		}
	}
	for len(s.rowToPatient) <= maxRow {
		s.rowToPatient = append(s.rowToPatient, "")
	}
	for pid, idx := range rows {
		for _, r := range idx {
			s.rowToPatient[r] = pid
		}
	}
	raw, _ := json.Marshal(s.rowToPatient)
	return os.WriteFile(path, raw, 0o644)
}

// extend row_to_patient.json, mapping each row index to patientID
func (s *Server) updateRowToPatient(cancelDir, patientID string, rowIndices []int) error {
	s.rowToPatientMu.Lock()
	defer s.rowToPatientMu.Unlock()
	path := filepath.Join(cancelDir, "row_to_patient.json")
	// load once, then mutate the mirror: re-reading on every registration is
	// an O(n) BeeGFS read per patient
	if !s.rowToPatientLoaded {
		if raw, err := os.ReadFile(path); err == nil {
			json.Unmarshal(raw, &s.rowToPatient) //nolint:errcheck
		}
		s.rowToPatientLoaded = true
	}
	maxRow := 0
	for _, r := range rowIndices {
		if r > maxRow {
			maxRow = r
		}
	}
	for len(s.rowToPatient) <= maxRow {
		s.rowToPatient = append(s.rowToPatient, "")
	}
	for _, r := range rowIndices {
		s.rowToPatient[r] = patientID
	}
	raw, _ := json.Marshal(s.rowToPatient)
	return os.WriteFile(path, raw, 0o644)
}

// qd's this patient has not signed yet
func (s *Server) handleConsentPending(w http.ResponseWriter, r *http.Request) {
	patientID := r.URL.Query().Get("patient_id")
	s.mu.Lock()
	var pending []map[string]string
	for qd, pq := range s.pending {
		if _, signed := pq.Sigs[patientID]; !signed {
			pending = append(pending, map[string]string{"qd": qd})
		}
	}
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{"pending": pending})
}

// record one patient's signature for a qd
func (s *Server) handleConsentSign(w http.ResponseWriter, r *http.Request) {
	var req struct {
		PatientID string `json:"patient_id"`
		QD        string `json:"qd"`
		SigHex    string `json:"sig_hex"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	// snapshot under the lock, then verify outside it: the BLS pairing must
	// not serialise the other concurrent /consent/sign handlers, and both
	// fields are written once at registration and never mutated
	s.mu.Lock()
	pat, patOK := s.patients[req.PatientID]
	var pkHex, cancellationCTHex string
	if patOK {
		pkHex, cancellationCTHex = pat.PKHex, pat.CancellationCTHex
	}
	s.mu.Unlock()
	if !patOK {
		w.WriteHeader(http.StatusOK)
		return
	}
	pkBytes, _ := hex.DecodeString(pkHex)
	sigBytes, _ := hex.DecodeString(req.SigHex)
	msg, msgErr := patient.EnvelopeMessage(req.QD, cancellationCTHex)
	if msgErr != nil || len(pkBytes) != blssig.PublicKeySize ||
		!blssig.Verify(pkBytes, msg, sigBytes) {
		w.WriteHeader(http.StatusOK)
		return
	}

	s.mu.Lock()
	if pq, ok := s.pending[req.QD]; ok {
		pq.Sigs[req.PatientID] = req.SigHex
	} else {
		// arrived before /query-request registered this qd, so stage it for
		// the registering goroutine to drain
		buf := s.prePending[req.QD]
		if buf == nil {
			buf = make(map[string]string)
			s.prePending[req.QD] = buf
		}
		buf[req.PatientID] = req.SigHex
	}
	s.mu.Unlock()
	s.cfg.Log.Info("consent received", zap.String("entity", "patient"),
		zap.String("patient_id", req.PatientID),
		zap.String("qd", req.QD[:8]+"…"))
	w.WriteHeader(http.StatusOK)
}

// signal that no more envelopes are coming for a qd
// collectConsent short-circuits on this, so partial-consent runs (E5 with
// consent_fraction < 1) do not wait out the fallback deadline
func (s *Server) handleConsentClose(w http.ResponseWriter, r *http.Request) {
	var req struct {
		QD string `json:"qd"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	if pq, ok := s.pending[req.QD]; ok {
		pq.Closed = true
	} else {
		// same race window as prePending, drained in handleQueryRequest
		s.preClose[req.QD] = true
	}
	s.mu.Unlock()
	w.WriteHeader(http.StatusOK)
}

// qd = SHA-256(json{instructions, run_id})
func makeQD(instructions, runID string) string {
	data, _ := json.Marshal(map[string]string{"instructions": instructions, "run_id": runID})
	h := sha256.Sum256(data)
	return hex.EncodeToString(h[:])
}

// Query request

// one POST /query-request
// everything but the encrypted EQ literals rides as a query parameter; the
// body is ct_v ← Enc_{pk_R}(v) (paper §3.4), which the proxy switches per
// provider via ksk_{R→L}. Range bounds stay plaintext on purpose: the
// Lagrange degree planning needs them at evaluation time
type QueryRequest struct {
	RunID        string
	Instructions string // semicolon-separated predicate template
	Rows         int
	Literals     []byte
	// return the single ct_R of app:protocol step 7 instead of one ciphertext
	// per provider; set once slot offsets are assigned and the backend can
	// place results at them
	Aggregate bool
}

// provider ID carrying the summed ct_R
const AggregateResultID = "aggregate"

// one provider's result or error
type queryResult struct {
	providerID string
	data       []byte
	bytes      int64
	err        error
}

// fan query out to providers
func (s *Server) handleQueryRequest(w http.ResponseWriter, r *http.Request) {
	literals, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "read body: "+err.Error(), http.StatusBadRequest)
		return
	}
	q := r.URL.Query()
	rows, _ := strconv.Atoi(q.Get("rows"))
	req := QueryRequest{
		RunID:        q.Get("run_id"),
		Instructions: q.Get("instructions"),
		Rows:         rows,
		Literals:     literals,
		Aggregate:    q.Get("aggregate") == "true",
	}

	s.mu.Lock()
	provs := make([]*provider, 0, len(s.providers))
	for _, p := range s.providers {
		provs = append(provs, p)
	}
	s.mu.Unlock()

	if len(provs) == 0 {
		http.Error(w, "no providers registered", http.StatusServiceUnavailable)
		return
	}

	totalStart := time.Now()

	// register qd as pending so patients can sign
	qd := makeQD(req.Instructions, req.RunID)
	s.cfg.Log.Info("query received", zap.String("entity", "proxy"), zap.String("run_id", req.RunID), zap.String("qd", qd[:8]+"…"), zap.Int("providers", len(provs)))
	s.mu.Lock()
	pq := &pendingQD{
		QD:           qd,
		Instructions: req.Instructions,
		CreatedAt:    time.Now(),
		Sigs:         make(map[string]string),
	}
	// drain signatures that arrived before this registration
	if buf := s.prePending[qd]; buf != nil {
		for pid, sig := range buf {
			pq.Sigs[pid] = sig
		}
		delete(s.prePending, qd)
	}
	// same race window for /consent/close
	if s.preClose[qd] {
		pq.Closed = true
		delete(s.preClose, qd)
	}
	s.pending[qd] = pq
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		delete(s.pending, qd)
		delete(s.prePending, qd)
		delete(s.preClose, qd)
		s.mu.Unlock()
	}()

	// key-switch the encrypted literals into each provider's domain before
	// fan-out: same predicates, each under its own sk_P
	switchedLiteralsByProvider := make(map[string]string)
	if len(req.Literals) > 0 {
		litRPath := filepath.Join(s.cfg.DataDir,
			fmt.Sprintf("literals_R_%s.bin", req.RunID))
		if err := os.WriteFile(litRPath, req.Literals, 0o600); err != nil {
			http.Error(w, "write literals_R: "+err.Error(), http.StatusInternalServerError)
			return
		}
		defer os.Remove(litRPath)
		baseMeta := metrics.ParseRunID(req.RunID)
		for _, p := range provs {
			if p.KSKRtoLPath == "" {
				s.cfg.Log.Warn("provider has no ksk_R->L; skipping literal switch",
					zap.String("entity", "proxy"), zap.String("provider", p.ID))
				continue
			}
			outPath := filepath.Join(s.cfg.DataDir,
				fmt.Sprintf("literals_L_%s_%s.bin", p.ID, req.RunID))
			tSwitch := time.Now()
			if err := s.cfg.Backend.ApplyKSKToLiterals(p.KSKRtoLPath, litRPath, outPath); err != nil {
				s.cfg.Log.Warn("ApplyKSKToLiterals failed",
					zap.String("entity", "proxy"), zap.String("provider", p.ID), zap.Error(err))
				continue
			}
			meta := baseMeta
			meta.Status = p.ID
			s.m.Record("proxy-apply-ksk-literals", time.Since(tSwitch), meta, 0, 0)
			switchedLiteralsByProvider[p.ID] = outPath
			defer os.Remove(outPath)
		}
	}

	resultCh := make(chan queryResult, len(provs))
	var wg sync.WaitGroup
	for _, p := range provs {
		wg.Add(1)
		go func(p *provider) {
			defer wg.Done()
			litPath := switchedLiteralsByProvider[p.ID]
			res, n, err := s.queryProvider(req, p, litPath)
			resultCh <- queryResult{providerID: p.ID, data: res, bytes: n, err: err}
		}(p)
	}

	// consent collection runs alongside provider evaluation and returns as
	// soon as everyone has signed; the deadline only catches a wedged patient
	// client, so it must outlive a full he3db evaluation
	consentTimeout := 48 * time.Hour
	consentDone := make(chan []string, 1)
	go func() { consentDone <- s.collectConsent(qd, consentTimeout) }()

	wg.Wait()
	close(resultCh)
	consentedPatients := <-consentDone

	s.cfg.Log.Info("consent collected",
		zap.String("entity", "proxy"),
		zap.String("qd", qd[:8]+"…"),
		zap.Int("consented", len(consentedPatients)))

	// σ_qd aggregation (paper app:protocol step 3, §A.1 phase iii): fold the
	// {σ_p}_{p∈C} into one 48-byte G1 multi-signature and check it in a single
	// pairing product
	// DEFERRED: (σ_qd, qd, C) is verified in memory and discarded rather than
	// retained as the audit transcript of paper step 7
	s.aggregateAndVerifyConsent(qd, req.RunID)

	// the canonical protocol ships one ct_R, formed as
	//   ct_R = KS(ksk_{h→u}, Filter_C(ct_B) + Σ_{p∈C} ct_{C,p})
	// summed across providers, which crypto.OffsetAggregator backends do by
	// folding each provider's slot rotation into its key-switching key. The
	// per-provider list below is the fallback for backends without that and
	// for federations whose rows exceed one slot row
	type aggResult struct {
		ProviderID string `json:"provider_id"`
		Data       []byte `json:"data,omitempty"`
		Error      string `json:"error,omitempty"`
	}
	var results []aggResult
	var totalNetBytes int64
	cancellationsDir := filepath.Join(s.cfg.DataDir, "cancellations")

	// row numbering is federation-wide, so every provider's cancellation and
	// Filter_C must be scoped to its own row range whether or not the results
	// are summed; summing additionally needs the user to ask for it and the
	// backend to place results at offsets
	var offsetAgg crypto.OffsetAggregator
	var switchedPaths []string
	if oa, ok := s.cfg.Backend.(crypto.OffsetAggregator); ok {
		offsetAgg = oa
	}
	aggregate := req.Aggregate && offsetAgg != nil
	if req.Aggregate && offsetAgg == nil {
		s.cfg.Log.Warn("aggregate requested but backend cannot place offsets",
			zap.String("entity", "proxy"))
	}

	for res := range resultCh {
		totalNetBytes += res.bytes
		if res.err != nil {
			s.cfg.Log.Warn("provider query failed",
				zap.String("entity", "proxy"), zap.String("provider", res.providerID), zap.Error(res.err))
			results = append(results, aggResult{ProviderID: res.providerID, Error: res.err.Error()})
			continue
		}

		tmpRaw := filepath.Join(s.cfg.DataDir,
			fmt.Sprintf("raw_%s_%s.bin", res.providerID, req.RunID))
		tmpCancelled := filepath.Join(s.cfg.DataDir,
			fmt.Sprintf("cancelled_%s_%s.bin", res.providerID, req.RunID))
		tmpSwitched := filepath.Join(s.cfg.DataDir,
			fmt.Sprintf("switched_%s_%s.bin", res.providerID, req.RunID))
		defer os.Remove(tmpRaw)
		defer os.Remove(tmpCancelled)
		defer os.Remove(tmpSwitched)

		if err := os.WriteFile(tmpRaw, res.data, 0o600); err != nil {
			results = append(results, aggResult{ProviderID: res.providerID, Error: "write tmp: " + err.Error()})
			continue
		}

		prov := s.providerByID(res.providerID)
		if prov == nil {
			results = append(results, aggResult{ProviderID: res.providerID, Error: "provider not found"})
			continue
		}

		tCan := time.Now()
		// called even with an empty consent set: the backend still applies
		// Filter_C, which zeroes every slot, whereas skipping would return the
		// blinded result verbatim
		cancelledPath := tmpRaw
		cancErr := error(nil)
		provConsent := s.consentForProvider(consentedPatients, prov.RowOffset, req.Rows)
		if offsetAgg != nil {
			cancErr = offsetAgg.ApplyCancellationAtOffset(
				tmpRaw, cancellationsDir, provConsent, tmpCancelled, prov.RowOffset)
		} else {
			cancErr = s.cfg.Backend.ApplyCancellation(
				tmpRaw, cancellationsDir, provConsent, tmpCancelled)
		}
		if cancErr != nil {
			s.cfg.Log.Warn("ApplyCancellation failed, skipping",
				zap.String("entity", "proxy"), zap.String("provider", res.providerID), zap.Error(cancErr))
		} else {
			cancelledPath = tmpCancelled
		}
		s.m.Record("proxy-cancellation", time.Since(tCan),
			metrics.Meta{Status: res.providerID}, 0, 0)

		// key-switch into the user domain
		tAgg := time.Now()
		var aggErr error
		if aggregate {
			aggErr = offsetAgg.AggregateAtOffset(prov.KSKPath, cancelledPath, tmpSwitched, prov.RowOffset)
		} else {
			aggErr = s.cfg.Backend.Aggregate(prov.KSKPath, cancelledPath, tmpSwitched)
		}
		if aggErr != nil {
			results = append(results, aggResult{ProviderID: res.providerID, Error: "aggregate: " + aggErr.Error()})
			continue
		}
		s.m.Record("proxy-aggregate", time.Since(tAgg), metrics.Meta{Status: res.providerID}, 0, 0)

		if aggregate {
			switchedPaths = append(switchedPaths, tmpSwitched)
			continue
		}

		switched, err := os.ReadFile(tmpSwitched)
		if err != nil {
			results = append(results, aggResult{ProviderID: res.providerID, Error: "read switched: " + err.Error()})
			continue
		}
		results = append(results, aggResult{ProviderID: res.providerID, Data: switched})
	}

	// cross-provider summation (paper app:protocol step 7): each ciphertext
	// already sits at its own slot offset, so adding them yields ct_R without
	// exposing the per-provider boundaries
	if aggregate && len(switchedPaths) > 0 {
		sumPath := filepath.Join(s.cfg.DataDir, fmt.Sprintf("ct_R_%s.bin", req.RunID))
		defer os.Remove(sumPath)
		tSum := time.Now()
		if err := offsetAgg.SumResults(switchedPaths, sumPath); err != nil {
			results = append(results, aggResult{ProviderID: AggregateResultID, Error: "sum: " + err.Error()})
		} else {
			s.m.Record("proxy-sum-results", time.Since(tSum),
				metrics.Meta{Status: fmt.Sprintf("providers=%d", len(switchedPaths))}, 0, 0)
			data, err := os.ReadFile(sumPath)
			if err != nil {
				results = append(results, aggResult{ProviderID: AggregateResultID, Error: "read ct_R: " + err.Error()})
			} else {
				results = append(results, aggResult{ProviderID: AggregateResultID, Data: data})
			}
		}
	}

	qMeta := metrics.ParseRunID(req.RunID)
	qMeta.Status = fmt.Sprintf("providers=%d,consented=%d", len(provs), len(consentedPatients))
	s.m.Record("proxy-query-total", time.Since(totalStart), qMeta, totalNetBytes, 0)

	s.cfg.Log.Info("results aggregated", zap.String("entity", "proxy"), zap.String("run_id", req.RunID), zap.Int("results", len(results)), zap.Int64("net_bytes", totalNetBytes))
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(results)
}

// narrow the consent set to one provider's row range
// consent is federation-wide and the cancellation store is shared across
// providers, so without this every provider would be handed every other
// provider's cancellations
func (s *Server) consentForProvider(consented []string, offset, rows int) []string {
	if rows <= 0 {
		return consented
	}
	out := make([]string, 0, len(consented))
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, pid := range consented {
		pat := s.patients[pid]
		if pat == nil {
			continue
		}
		for _, row := range pat.RecordIDs {
			if row >= offset && row < offset+rows {
				out = append(out, pid)
				break
			}
		}
	}
	return out
}

// fold the collected signatures into σ_multi and verify it
// aggregation and verification are timed separately so the evaluation can
// isolate the BLS benefit from the per-patient sign cost
func (s *Server) aggregateAndVerifyConsent(qd, runID string) {
	s.mu.Lock()
	pq := s.pending[qd]
	if pq == nil || len(pq.Sigs) == 0 {
		s.mu.Unlock()
		return
	}
	pks := make([][]byte, 0, len(pq.Sigs))
	sigs := make([][]byte, 0, len(pq.Sigs))
	msgs := make([][]byte, 0, len(pq.Sigs))
	for pid, sigHex := range pq.Sigs {
		pat := s.patients[pid]
		if pat == nil {
			continue
		}
		pkBytes, _ := hex.DecodeString(pat.PKHex)
		sigBytes, _ := hex.DecodeString(sigHex)
		if len(pkBytes) != blssig.PublicKeySize || len(sigBytes) != blssig.SignatureSize {
			continue
		}
		msg, err := patient.EnvelopeMessage(qd, pat.CancellationCTHex)
		if err != nil {
			continue
		}
		pks = append(pks, pkBytes)
		sigs = append(sigs, sigBytes)
		msgs = append(msgs, msg)
	}
	s.mu.Unlock()

	if len(sigs) == 0 {
		return
	}

	meta := metrics.ParseRunID(runID)
	meta.Status = fmt.Sprintf("n=%d", len(sigs))

	tAgg := time.Now()
	aggSig, err := blssig.Aggregate(sigs)
	if err != nil {
		s.cfg.Log.Warn("BLS aggregate failed",
			zap.String("entity", "proxy"), zap.Error(err))
		return
	}
	s.m.Record("proxy-bls-aggregate", time.Since(tAgg), meta,
		int64(len(sigs)*blssig.SignatureSize), int64(len(aggSig)))

	tVer := time.Now()
	ok := blssig.VerifyAggregate(pks, msgs, aggSig)
	s.m.Record("proxy-bls-verify-aggregate", time.Since(tVer), meta, 0, 0)
	if !ok {
		s.cfg.Log.Warn("BLS aggregate verification failed",
			zap.String("entity", "proxy"), zap.Int("sigs", len(sigs)))
	}
}

// wait up to timeout for envelope signatures on qd
// returns whoever signed, falling back to every registered patient if none
// arrived — the legacy bench path that skips the patient role, which
// timeout=0 selects outright
func (s *Server) collectConsent(qd string, timeout time.Duration) []string {
	if timeout == 0 {
		return s.allRegisteredPatients()
	}

	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		s.mu.Lock()
		pq := s.pending[qd]
		numPatients := len(s.patients)
		var consented []string
		closed := false
		if pq != nil {
			closed = pq.Closed
			for pid := range pq.Sigs {
				consented = append(consented, pid)
			}
		}
		s.mu.Unlock()
		// exit early once the bench is done pushing (consent_fraction < 1 in
		// E5) or everyone has signed (consent_fraction == 1)
		if closed {
			return consented
		}
		if numPatients > 0 && len(consented) >= numPatients {
			return consented
		}
		time.Sleep(100 * time.Millisecond)
	}
	s.mu.Lock()
	pq := s.pending[qd]
	var consented []string
	if pq != nil {
		for pid := range pq.Sigs {
			consented = append(consented, pid)
		}
	}
	s.mu.Unlock()
	if len(consented) > 0 {
		return consented
	}
	return s.allRegisteredPatients()
}

func (s *Server) allRegisteredPatients() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]string, 0, len(s.patients))
	for pid := range s.patients {
		out = append(out, pid)
	}
	return out
}

func (s *Server) providerByID(id string) *provider {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.providers[id]
}

// send the predicate to one provider and wait for its blinded result
func (s *Server) queryProvider(req QueryRequest, p *provider, literalsPath string) ([]byte, int64, error) {
	var litBytes []byte
	if literalsPath != "" {
		var err error
		litBytes, err = os.ReadFile(literalsPath)
		if err != nil {
			return nil, 0, fmt.Errorf("read switched literals: %w", err)
		}
	}
	// raw body, not hex-in-JSON: that doubled the per-query volume on this link
	q := url.Values{"run_id": {req.RunID}, "instructions": {req.Instructions}}
	netOut := int64(len(litBytes))
	tEval := time.Now()
	resp, err := http.Post(p.Address+"/evaluate-query?"+q.Encode(),
		"application/octet-stream", bytes.NewReader(litBytes))
	if err != nil {
		return nil, 0, fmt.Errorf("evaluate-query: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, 0, fmt.Errorf("evaluate-query: status %d: %s", resp.StatusCode, body)
	}
	var evalResp struct {
		JobID string `json:"job_id"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&evalResp); err != nil {
		return nil, 0, fmt.Errorf("evaluate-query: decode job_id: %w", err)
	}

	resultBytes, n, err := s.pollProviderResult(p, evalResp.JobID)
	if err != nil {
		return nil, 0, err
	}
	eMeta := metrics.ParseRunID(req.RunID)
	eMeta.Status = p.ID
	s.m.Record("proxy-provider-eval", time.Since(tEval), eMeta, netOut, n)
	return resultBytes, n, nil
}

// poll /query-status until the job is done
// the deadline only catches a wedged provider (CGO deadlock, infinite loop):
// legitimate evaluations run for hours, so it sits beyond any SLURM walltime
func (s *Server) pollProviderResult(p *provider, jobID string) ([]byte, int64, error) {
	const (
		pollInterval = 500 * time.Millisecond
		maxWait      = 48 * time.Hour
	)
	deadline := time.Now().Add(maxWait)
	for time.Now().Before(deadline) {
		resp, err := http.Get(p.Address + "/query-status?job_id=" + jobID)
		if err != nil {
			time.Sleep(pollInterval)
			continue
		}
		var status struct {
			State string `json:"state"`
			Error string `json:"error"`
		}
		json.NewDecoder(resp.Body).Decode(&status)
		resp.Body.Close()

		switch status.State {
		case "done":
			r2, err := http.Get(p.Address + "/get-result?job_id=" + jobID)
			if err != nil {
				return nil, 0, fmt.Errorf("get-result: %w", err)
			}
			defer r2.Body.Close()
			data, err := io.ReadAll(r2.Body)
			return data, int64(len(data)), err
		case "error":
			s.cfg.Log.Error("provider job failed",
				zap.String("entity", "proxy"),
				zap.String("provider", p.ID),
				zap.String("job", jobID),
				zap.String("provider_error", status.Error))
			return nil, 0, fmt.Errorf("provider reported job error for %s", jobID)
		}
		time.Sleep(pollInterval)
	}
	return nil, 0, fmt.Errorf("timeout waiting for provider job %s", jobID)
}

// In-process convenience (used by bench run-task)

// register an in-process provider
func (s *Server) RegisterLocal(id, address, skProviderDir, skResearcherDir string) error {
	kskPath := filepath.Join(s.cfg.DataDir, fmt.Sprintf("ksk_%s.bin", id))
	if err := s.cfg.Backend.GenerateKSK(skResearcherDir, skProviderDir, kskPath); err != nil {
		return fmt.Errorf("RegisterLocal KSK: %w", err)
	}
	s.mu.Lock()
	s.providers[id] = &provider{ID: id, Address: address, SKDir: skProviderDir, KSKPath: kskPath}
	s.mu.Unlock()
	return nil
}

// poll /health until the server at addr responds
func WaitReady(addr string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		resp, err := http.Get(addr + "/health")
		if err == nil && resp.StatusCode == http.StatusOK {
			resp.Body.Close()
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("server at %s not ready after %s", addr, timeout)
}

// register a consenting patient without the HTTP enrollment round-trip
// writes no cancellation CT: the caller must have placed
// {cancellationsDir}/{patientID}.bin via Backend.SetupPatientBlinding
func (s *Server) RegisterPatientLocal(patientID string, recordIDs []int) {
	s.mu.Lock()
	s.patients[patientID] = &patientRecord{PatientID: patientID, RecordIDs: recordIDs}
	s.mu.Unlock()
}

func (s *Server) ProviderCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.providers)
}

// "rows" query param, with a default
func rowsParam(r *http.Request, def int) int {
	v := r.URL.Query().Get("rows")
	n, err := strconv.Atoi(v)
	if err != nil || n <= 0 {
		return def
	}
	return n
}
