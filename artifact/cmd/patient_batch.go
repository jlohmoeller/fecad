package cmd

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"runtime"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/metrics"
	"github.com/fecad/internal/patient"
)

// patient subprocess command
// bench spawns one per task so VmHWM here is the patient role's peak RSS
// alone, not the bench process's mixed footprint
var patientBatchCmd = &cobra.Command{
	Use:   "batch",
	Short: "Patient batch subprocess helpers",
}

var patientBatchServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Run a long-lived patient subprocess that handles one patient at a time",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		addr, _ := cmd.Flags().GetString("addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		if err := os.MkdirAll(dataDir, 0o755); err != nil {
			return fmt.Errorf("mkdir data dir: %w", err)
		}

		s := &patientBatchServer{
			clients: make(map[string]*patient.Client),
			mw:      metrics.NewWriter(dataDir),
			log:     log,
		}
		r := chi.NewRouter()
		r.Get("/health", func(w http.ResponseWriter, _ *http.Request) { w.WriteHeader(http.StatusOK) })
		r.Post("/enroll", s.handleEnroll)
		r.Post("/enroll-batch", s.handleEnrollBatch)
		r.Post("/sign", s.handleSign)
		r.Post("/sign-all", s.handleSignAll)

		ln, err := net.Listen("tcp", addr)
		if err != nil {
			return fmt.Errorf("listen %s: %w", addr, err)
		}
		// The parent reads this line to learn the OS-assigned port
		fmt.Fprintf(os.Stdout, "LISTENING addr=%s\n", ln.Addr().String())
		_ = os.Stdout.Sync()
		log.Info("patient batch listening", zap.String("addr", ln.Addr().String()))
		return http.Serve(ln, r)
	},
}

type patientBatchServer struct {
	mu      sync.Mutex
	clients map[string]*patient.Client
	mw      *metrics.Writer
	log     *zap.Logger
}

type enrollReq struct {
	PatientID         string `json:"patient_id"`
	RecordIDs         []int  `json:"record_ids"`
	ProxyURL          string `json:"proxy_url"`
	RunID             string `json:"run_id"`
	CancellationCTHex string `json:"cancellation_ct_hex"`
}

type signReq struct {
	PatientID string `json:"patient_id"`
	ProxyURL  string `json:"proxy_url"`
	QD        string `json:"qd"`
	RunID     string `json:"run_id"`
}

// batch enrollment request
// n BLS keypairs here, one registration to the proxy (paper §6.3)
type enrollBatchReq struct {
	Patients []struct {
		PatientID         string `json:"patient_id"`
		RecordIDs         []int  `json:"record_ids"`
		CancellationCTHex string `json:"cancellation_ct_hex"`
	} `json:"patients"`
	ProxyURL string `json:"proxy_url"`
	RunID    string `json:"run_id"`
}

// batch envelope-sign request
// σ_p = BLS.Sign(sk_p, H(qd ‖ ct_C)), shipped in one request so the
// measurement is signing cost, not round-trips
type signAllReq struct {
	ProxyURL string `json:"proxy_url"`
	QD       string `json:"qd"`
	RunID    string `json:"run_id"`
}

func (s *patientBatchServer) handleEnroll(w http.ResponseWriter, r *http.Request) {
	var req enrollReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	c, err := patient.NewClient(req.PatientID, req.RecordIDs)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	c.CancellationCTHex = req.CancellationCTHex

	body, _ := json.Marshal(map[string]any{
		"patient_id":          c.PatientID,
		"pk_hex":              hex.EncodeToString(c.PK),
		"cancellation_ct_hex": c.CancellationCTHex,
		"record_ids":          c.RecordIDs,
	})

	meta := metrics.ParseRunID(req.RunID)
	meta.Status = req.PatientID
	t := time.Now()
	resp, err := http.Post(req.ProxyURL+"/consent/register-patient", "application/json", bytes.NewReader(body))
	if err != nil {
		http.Error(w, "proxy register: "+err.Error(), http.StatusBadGateway)
		return
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		http.Error(w, fmt.Sprintf("proxy register: status %d", resp.StatusCode), http.StatusBadGateway)
		return
	}
	s.mw.Record("patient-consent", time.Since(t), meta, int64(len(body)), 0)

	s.mu.Lock()
	s.clients[c.PatientID] = c
	s.mu.Unlock()

	w.WriteHeader(http.StatusOK)
}

func (s *patientBatchServer) handleEnrollBatch(w http.ResponseWriter, r *http.Request) {
	var req enrollBatchReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = fmt.Sprintf("n=%d", len(req.Patients))

	t := time.Now()
	clients := make([]*patient.Client, len(req.Patients))
	regs := make([]map[string]any, len(req.Patients))
	var wg sync.WaitGroup
	sem := make(chan struct{}, runtime.NumCPU())
	errs := make([]error, len(req.Patients))
	for i, p := range req.Patients {
		wg.Add(1)
		go func(i int, id string, rows []int, cancHex string) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			c, err := patient.NewClient(id, rows)
			if err != nil {
				errs[i] = err
				return
			}
			c.CancellationCTHex = cancHex
			clients[i] = c
			regs[i] = map[string]any{
				"patient_id":          c.PatientID,
				"pk_hex":              hex.EncodeToString(c.PK),
				"cancellation_ct_hex": c.CancellationCTHex,
				"record_ids":          c.RecordIDs,
			}
		}(i, p.PatientID, p.RecordIDs, p.CancellationCTHex)
	}
	wg.Wait()
	for _, err := range errs {
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}

	body, _ := json.Marshal(map[string]any{"patients": regs})
	resp, err := http.Post(req.ProxyURL+"/consent/register-batch", "application/json", bytes.NewReader(body))
	if err != nil {
		http.Error(w, "proxy register-batch: "+err.Error(), http.StatusBadGateway)
		return
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		http.Error(w, fmt.Sprintf("proxy register-batch: status %d", resp.StatusCode), http.StatusBadGateway)
		return
	}
	s.mw.Record("patient-consent", time.Since(t), meta, int64(len(body)), 0)

	s.mu.Lock()
	for _, c := range clients {
		s.clients[c.PatientID] = c
	}
	s.mu.Unlock()
	w.WriteHeader(http.StatusOK)
}

func (s *patientBatchServer) handleSignAll(w http.ResponseWriter, r *http.Request) {
	var req signAllReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	clients := make([]*patient.Client, 0, len(s.clients))
	for _, c := range s.clients {
		clients = append(clients, c)
	}
	s.mu.Unlock()
	if len(clients) == 0 {
		w.WriteHeader(http.StatusOK)
		return
	}

	meta := metrics.ParseRunID(req.RunID)
	meta.Status = fmt.Sprintf("n=%d", len(clients))

	tBatch := time.Now()
	sigs := make([]map[string]string, len(clients))
	errs := make([]error, len(clients))
	var wg sync.WaitGroup
	sem := make(chan struct{}, runtime.NumCPU())
	tSign := time.Now()
	for i, c := range clients {
		wg.Add(1)
		go func(i int, c *patient.Client) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			sig, err := c.EnvelopeSign(req.QD)
			if err != nil {
				errs[i] = err
				return
			}
			sigs[i] = map[string]string{"patient_id": c.PatientID, "sig_hex": hex.EncodeToString(sig)}
		}(i, c)
	}
	wg.Wait()
	signDur := time.Since(tSign) / time.Duration(len(clients))
	for _, err := range errs {
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}

	body, _ := json.Marshal(map[string]any{"qd": req.QD, "signatures": sigs})
	tPost := time.Now()
	resp, err := http.Post(req.ProxyURL+"/consent/sign-batch", "application/json", bytes.NewReader(body))
	if err != nil {
		http.Error(w, "proxy sign-batch: "+err.Error(), http.StatusBadGateway)
		return
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		http.Error(w, fmt.Sprintf("proxy sign-batch: status %d", resp.StatusCode), http.StatusBadGateway)
		return
	}
	// Patients sign independently in a deployment, so the critical-path cost is
	// one signature plus the hand-off, not wall-clock for signing all of them
	postDur := time.Since(tPost)
	s.mw.Record("patient-envelope-batch", signDur+postDur, meta, int64(len(body)), 0)
	s.mw.Record("patient-envelope-sign-avg", signDur, meta, 0, 0)
	s.log.Info("envelopes signed", zap.Int("n", len(clients)),
		zap.Duration("sign_wall", time.Since(tBatch)-postDur),
		zap.Duration("sign_avg", signDur))
	w.WriteHeader(http.StatusOK)
}

func (s *patientBatchServer) handleSign(w http.ResponseWriter, r *http.Request) {
	var req signReq
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	c := s.clients[req.PatientID]
	s.mu.Unlock()
	if c == nil {
		http.Error(w, "patient not enrolled: "+req.PatientID, http.StatusNotFound)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "n=1"

	tBatch := time.Now()
	tSign := time.Now()
	sig, err := c.EnvelopeSign(req.QD)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	signDur := time.Since(tSign)
	body, _ := json.Marshal(map[string]string{
		"patient_id": c.PatientID,
		"qd":         req.QD,
		"sig_hex":    hex.EncodeToString(sig),
	})
	resp, err := http.Post(req.ProxyURL+"/consent/sign", "application/json", bytes.NewReader(body))
	if err != nil {
		http.Error(w, "proxy sign: "+err.Error(), http.StatusBadGateway)
		return
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		http.Error(w, fmt.Sprintf("proxy sign: status %d", resp.StatusCode), http.StatusBadGateway)
		return
	}
	s.mw.Record("patient-envelope-batch", time.Since(tBatch), meta, int64(len(body)), 0)
	s.mw.Record("patient-envelope-sign-avg", signDur, meta, 0, 0)

	w.WriteHeader(http.StatusOK)
}

func init() {
	patientBatchServeCmd.Flags().String("addr", "127.0.0.1:0", "listen address (use :0 for OS-assigned port)")
	patientBatchServeCmd.Flags().String("data-dir", "data/patient", "directory for performance_metrics.csv")
	patientBatchCmd.AddCommand(patientBatchServeCmd)
	patientCmd.AddCommand(patientBatchCmd)
}
