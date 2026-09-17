// Package patient implements the FeCaD Patient service
// the patient holds a BLS12-381 keypair, samples r_p at enrollment, and polls
// for query descriptors to sign; the proxy folds the signatures into one
// 48-byte multi-signature per query (paper §3.3, §A.1)
package patient

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"go.uber.org/zap"

	"github.com/fecad/internal/blssig"
	"github.com/fecad/internal/metrics"
)

// server parameters
type Config struct {
	DataDir     string
	PatientID   string
	ProxyURL    string // e.g. "http://proxy:8082"
	ProviderURL string // e.g. "http://provider:8081"
	RecordIDs   []int  // row indices this patient owns in the provider dataset
	RunID       string // benchmark run identifier, e.g. "task0_nuclear_medicine_q1"
	Log         *zap.Logger
}

type Server struct {
	cfg      Config
	m        *metrics.Writer
	r        *chi.Mux
	mu       sync.Mutex
	sk       []byte          // BLS12-381 private scalar (32 bytes)
	pk       []byte          // BLS12-381 G2 public key (96 bytes)
	policy   map[string]bool // qd hex (or "*") -> allow
	enrolled bool
}

// call Start once the server is listening
func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0o755); err != nil {
		cfg.Log.Warn("mkdir data dir", zap.Error(err))
	}
	s := &Server{cfg: cfg, m: metrics.NewWriter(cfg.DataDir), policy: map[string]bool{"*": true}}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Get("/pk", s.handlePK)
	s.r.Post("/set-policy", s.handleSetPolicy)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

// launch enrollment + polling goroutines
func (s *Server) Start() { go s.background() }

func (s *Server) background() {
	if err := s.loadOrGenKeyPair(); err != nil {
		s.cfg.Log.Error("patient keypair init failed", zap.Error(err))
		return
	}
	if s.cfg.ProviderURL != "" && s.cfg.ProxyURL != "" && len(s.cfg.RecordIDs) > 0 {
		for attempt := range 10 {
			if err := s.enroll(); err != nil {
				s.cfg.Log.Warn("enrollment attempt failed",
					zap.Int("attempt", attempt+1), zap.Error(err))
				time.Sleep(5 * time.Second)
				continue
			}
			s.mu.Lock()
			s.enrolled = true
			s.mu.Unlock()
			s.cfg.Log.Info("patient enrolled",
				zap.String("patient_id", s.cfg.PatientID))
			break
		}
	}
	go s.pollLoop()
}

func (s *Server) loadOrGenKeyPair() error {
	skPath := s.cfg.DataDir + "/patient_sk.bin"
	pkPath := s.cfg.DataDir + "/patient_pk.bin"
	if skData, err := os.ReadFile(skPath); err == nil && len(skData) == blssig.PrivateKeySize {
		if pkData, err2 := os.ReadFile(pkPath); err2 == nil && len(pkData) == blssig.PublicKeySize {
			s.mu.Lock()
			s.sk = skData
			s.pk = pkData
			s.mu.Unlock()
			return nil
		}
	}
	t := time.Now()
	sk, pk, err := blssig.GenerateKey()
	if err != nil {
		return fmt.Errorf("bls keygen: %w", err)
	}
	if err := os.WriteFile(skPath, sk, 0o600); err != nil {
		return fmt.Errorf("write sk: %w", err)
	}
	if err := os.WriteFile(pkPath, pk, 0o644); err != nil {
		return fmt.Errorf("write pk: %w", err)
	}
	meta := metrics.ParseRunID(s.cfg.RunID)
	meta.Status = s.cfg.PatientID
	s.m.Record("patient-keygen", time.Since(t), meta, 0, 0)
	s.mu.Lock()
	s.sk = sk
	s.pk = pk
	s.mu.Unlock()
	return nil
}

// enroll at the provider, then register the cancellation CT at the proxy
func (s *Server) enroll() error {
	s.mu.Lock()
	pk := s.pk
	s.mu.Unlock()

	t := time.Now()

	// 1. provider generates the blinding CTs
	body, _ := json.Marshal(map[string]any{
		"patient_id": s.cfg.PatientID,
		"pk_hex":     hex.EncodeToString(pk),
		"record_ids": s.cfg.RecordIDs,
	})
	resp, err := http.Post(s.cfg.ProviderURL+"/enroll-patient",
		"application/json", bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("enroll provider: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("enroll provider: status %d", resp.StatusCode)
	}
	var enrollResp struct {
		CancellationCTHex string `json:"cancellation_ct_hex"`
	}
	respBytes, _ := io.ReadAll(resp.Body)
	if err := json.Unmarshal(respBytes, &enrollResp); err != nil {
		return fmt.Errorf("enroll provider: decode: %w", err)
	}

	// 2. register at the proxy, record_ids drive the row mapping
	regBody, _ := json.Marshal(map[string]any{
		"patient_id":          s.cfg.PatientID,
		"pk_hex":              hex.EncodeToString(pk),
		"cancellation_ct_hex": enrollResp.CancellationCTHex,
		"record_ids":          s.cfg.RecordIDs,
	})
	resp2, err := http.Post(s.cfg.ProxyURL+"/consent/register-patient",
		"application/json", bytes.NewReader(regBody))
	if err != nil {
		return fmt.Errorf("register proxy: %w", err)
	}
	resp2.Body.Close()
	if resp2.StatusCode != http.StatusOK {
		return fmt.Errorf("register proxy: status %d", resp2.StatusCode)
	}

	meta := metrics.ParseRunID(s.cfg.RunID)
	meta.Status = s.cfg.PatientID
	// out is dominated by the cancellation CT on the register hop,
	// in is the enroll response carrying that same CT as hex
	outBytes := int64(len(body) + len(regBody))
	inBytes := int64(len(respBytes))
	s.m.Record("patient-enroll", time.Since(t), meta, outBytes, inBytes)
	return nil
}

// sign pending qd's allowed by policy
func (s *Server) pollLoop() {
	for {
		s.mu.Lock()
		sk := s.sk
		s.mu.Unlock()
		if len(sk) > 0 && s.cfg.ProxyURL != "" {
			if err := s.fetchAndSign(sk); err != nil {
				s.cfg.Log.Debug("consent poll error", zap.Error(err))
			}
		}
		time.Sleep(5 * time.Second)
	}
}

func (s *Server) fetchAndSign(sk []byte) error {
	url := fmt.Sprintf("%s/consent/pending?patient_id=%s",
		s.cfg.ProxyURL, s.cfg.PatientID)
	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("pending: status %d", resp.StatusCode)
	}
	var result struct {
		Pending []struct {
			QD string `json:"qd"`
		} `json:"pending"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return err
	}
	for _, entry := range result.Pending {
		if !s.checkPolicy(entry.QD) {
			continue
		}
		tConsent := time.Now()
		qdBytes, err := hex.DecodeString(entry.QD)
		if err != nil {
			// qd is not a raw hex hash, so hash the string itself
			h := sha256.Sum256([]byte(entry.QD))
			qdBytes = h[:]
		}
		sig, err := blssig.Sign(sk, qdBytes)
		if err != nil {
			return fmt.Errorf("bls sign: %w", err)
		}
		sigBody, _ := json.Marshal(map[string]string{
			"patient_id": s.cfg.PatientID,
			"qd":         entry.QD,
			"sig_hex":    hex.EncodeToString(sig),
		})
		r2, err := http.Post(s.cfg.ProxyURL+"/consent/sign",
			"application/json", bytes.NewReader(sigBody))
		if err == nil {
			r2.Body.Close()
		}
		meta := metrics.ParseRunID(s.cfg.RunID)
		meta.Status = s.cfg.PatientID
		s.m.Record("patient-consent", time.Since(tConsent), meta, int64(len(sigBody)), 0)
	}
	return nil
}

func (s *Server) checkPolicy(qd string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if v, ok := s.policy[qd]; ok {
		return v
	}
	if v, ok := s.policy["*"]; ok {
		return v
	}
	return true
}

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	enrolled := s.enrolled
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{
		"status":     "ok",
		"patient_id": s.cfg.PatientID,
		"enrolled":   enrolled,
	})
}

func (s *Server) handlePK(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	pk := s.pk
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"pk_hex":     hex.EncodeToString(pk),
		"patient_id": s.cfg.PatientID,
	})
}

type setPolicyRequest struct {
	QD    string `json:"qd"`
	Allow bool   `json:"allow"`
}

func (s *Server) handleSetPolicy(w http.ResponseWriter, r *http.Request) {
	var req setPolicyRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.QD == "" {
		req.QD = "*"
	}
	s.mu.Lock()
	s.policy[req.QD] = req.Allow
	s.mu.Unlock()
	w.WriteHeader(http.StatusOK)
}
