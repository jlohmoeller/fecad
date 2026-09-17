// Package evaluator implements the evaluator half of the Provider role
// (paper app:protocol steps C and 5): it holds the ciphertext store, the
// evaluation key and the +r_p blindings, never sk_h, and may run on
// outsourced infrastructure
//
// by default it shares a data directory with the data holder and the role
// boundary is enforced only at the HTTP and package surface; under
// `bench --split-storage` it gets its own directory via /install-store so
// sk_h never touches its filesystem

package evaluator

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"go.uber.org/zap"

	"github.com/fecad/internal/crypto"
	"github.com/fecad/internal/keymanager"
	"github.com/fecad/internal/metrics"
)

// server parameters
type Config struct {
	DataDir    string
	SchemaPath string
	Backend    crypto.Backend
	Log        *zap.Logger
}

// one async evaluation job
type jobState struct {
	state     string // "running" | "done" | "error"
	result    string // file path of result_query.bin
	err       string
	bodyBytes int64         // /evaluate-query request body size (net_in for provider-evaluate)
	dur       time.Duration // evaluation wall clock
	runID     string        // for ParseRunID at result-serve time
}

type Server struct {
	cfg  Config
	m    *metrics.Writer
	r    *chi.Mux
	mu   sync.Mutex
	jobs map[string]*jobState
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0755); err != nil {
		cfg.Log.Warn("could not create data dir", zap.Error(err))
	}
	s := &Server{
		cfg:  cfg,
		m:    metrics.NewWriter(cfg.DataDir),
		jobs: make(map[string]*jobState),
	}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Post("/install-store", s.handleInstallStore)
	s.r.Post("/evaluate-query", s.handleEvaluateQuery)
	s.r.Get("/query-status", s.handleQueryStatus)
	s.r.Get("/get-result", s.handleGetResult)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.WriteHeader(http.StatusOK)
}

// pull the store from the data holder (paper app:protocol step C)
// one ingestion-time transfer, which is what a storage-disjoint deployment
// pays over the co-located one; the tar never carries sk_h
func (s *Server) handleInstallStore(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID         string `json:"run_id"`
		DataHolderURL string `json:"data_holder_url"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.DataHolderURL == "" {
		http.Error(w, "data_holder_url required", http.StatusBadRequest)
		return
	}

	t := time.Now()
	resp, err := http.Get(req.DataHolderURL + "/store-tar?run_id=" + url.QueryEscape(req.RunID))
	if err != nil {
		http.Error(w, "fetch store: "+err.Error(), http.StatusBadGateway)
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		http.Error(w, fmt.Sprintf("data holder status %d: %s", resp.StatusCode, body),
			http.StatusBadGateway)
		return
	}
	n, err := keymanager.ExtractTar(resp.Body, s.cfg.DataDir)
	if err != nil {
		http.Error(w, "extract: "+err.Error(), http.StatusInternalServerError)
		return
	}
	if _, statErr := os.Stat(s.cfg.DataDir + "/secret_key.bin"); statErr == nil {
		// the role split is the point of this deployment
		http.Error(w, "sk_h present at evaluator — role split violated",
			http.StatusInternalServerError)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "success"
	s.m.Record("evaluator-install-store", time.Since(t), meta, 0, n)
	s.cfg.Log.Info("store installed from data holder",
		zap.String("entity", "provider"),
		zap.String("run_id", req.RunID), zap.Int64("bytes", n))
	w.WriteHeader(http.StatusOK)
}

// one POST /evaluate-query
// run_id and instructions ride as query parameters, the body is the raw
// proxy-switched literal CT: hex-in-JSON doubled the per-query volume
type EvaluateRequest struct {
	RunID        string
	Instructions string
	Literals     []byte
}

func (s *Server) handleEvaluateQuery(w http.ResponseWriter, r *http.Request) {
	// body size is the provider-evaluate net_in, mirroring the
	// proxy-provider-eval net_out
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "read body: "+err.Error(), http.StatusBadRequest)
		return
	}
	req := EvaluateRequest{
		RunID:        r.URL.Query().Get("run_id"),
		Instructions: r.URL.Query().Get("instructions"),
		Literals:     raw,
	}

	jobID := fmt.Sprintf("%s_%d", req.RunID, time.Now().UnixNano())

	s.mu.Lock()
	s.jobs[jobID] = &jobState{state: "running", bodyBytes: int64(len(raw)), runID: req.RunID}
	s.mu.Unlock()

	go s.runEvalJob(jobID, req)

	s.cfg.Log.Info("evaluation queued", zap.String("entity", "provider"), zap.String("job", jobID), zap.String("run_id", req.RunID))
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"job_id": jobID})
}

// evaluate, then blind non-consenting rows (paper app:protocol step 5)
func (s *Server) runEvalJob(jobID string, req EvaluateRequest) {
	s.cfg.Log.Info("evaluation started",
		zap.String("entity", "provider"),
		zap.String("job", jobID),
		zap.String("instructions", req.Instructions))
	t := time.Now()
	// drop the eval key once evaluation is done: it is dead weight afterwards
	// and runs to several GB (~5.3 GB for he3db at 100 K rows)
	evalKeyPath := s.cfg.DataDir + "/eval_key.bin"
	defer func() {
		if info, statErr := os.Stat(evalKeyPath); statErr == nil {
			s.cfg.Log.Info("removing eval key after evaluation",
				zap.String("entity", "provider"),
				zap.String("job", jobID),
				zap.Int64("bytes", info.Size()))
			if rmErr := os.Remove(evalKeyPath); rmErr != nil {
				s.cfg.Log.Warn("failed to remove eval key",
					zap.String("entity", "provider"),
					zap.String("job", jobID), zap.Error(rmErr))
			}
		}
	}()
	var err error
	if len(req.Literals) > 0 {
		litPath := s.cfg.DataDir + "/literals_L.bin"
		if writeErr := os.WriteFile(litPath, req.Literals, 0o600); writeErr != nil {
			err = fmt.Errorf("write literals_L: %w", writeErr)
		} else {
			defer os.Remove(litPath)
			err = s.cfg.Backend.EvaluatePredicateEncLiterals(
				s.cfg.DataDir, litPath, req.Instructions)
		}
	} else {
		err = s.cfg.Backend.EvaluatePredicate(s.cfg.DataDir, req.Instructions)
	}
	if err == nil {
		// ct_B = ct_match + Σ ct_{B,p} (paper app:protocol step 5)
		blindingsDir := s.cfg.DataDir + "/blindings"
		rawPath := s.cfg.DataDir + "/result_query.bin"
		blindedPath := s.cfg.DataDir + "/result_blinded.bin"
		if berr := s.cfg.Backend.ApplyBlinding(rawPath, blindingsDir, blindedPath); berr != nil {
			s.cfg.Log.Warn("ApplyBlinding failed, returning unblinded result",
				zap.String("entity", "provider"), zap.String("job", jobID), zap.Error(berr))
			blindedPath = rawPath
		}
		_ = os.Rename(blindedPath, rawPath)
	}
	dur := time.Since(t)

	s.mu.Lock()
	job := s.jobs[jobID]
	if err != nil {
		job.state = "error"
		job.err = err.Error()
		s.cfg.Log.Error("EvaluatePredicate failed",
			zap.String("entity", "provider"),
			zap.String("job", jobID),
			zap.Duration("dur", dur),
			zap.Error(err))
	} else {
		job.state = "done"
		job.result = s.cfg.DataDir + "/result_query.bin"
		job.dur = dur
		s.cfg.Log.Info("evaluation complete",
			zap.String("entity", "provider"),
			zap.String("job", jobID),
			zap.Duration("dur", dur))
	}
	// provider-evaluate is recorded at /get-result instead, so one row can
	// carry both the eval body net_in and the result CT net_out
	s.mu.Unlock()
}

func (s *Server) handleQueryStatus(w http.ResponseWriter, r *http.Request) {
	jobID := r.URL.Query().Get("job_id")
	s.mu.Lock()
	job := s.jobs[jobID]
	s.mu.Unlock()

	if job == nil {
		http.Error(w, "job not found", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"job_id": jobID,
		"state":  job.state,
		"error":  job.err,
	})
}

func (s *Server) handleGetResult(w http.ResponseWriter, r *http.Request) {
	jobID := r.URL.Query().Get("job_id")
	s.mu.Lock()
	job := s.jobs[jobID]
	s.mu.Unlock()

	if job == nil || job.state != "done" {
		http.Error(w, "result not ready", http.StatusNotFound)
		return
	}
	data, err := os.ReadFile(job.result)
	if err != nil {
		http.Error(w, "result file missing", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	n, _ := w.Write(data)
	meta := metrics.ParseRunID(job.runID)
	meta.Status = "success"
	s.m.Record("provider-evaluate", job.dur, meta, int64(n), job.bodyBytes)
}
