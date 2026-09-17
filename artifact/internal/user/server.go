// Package user implements the FeCaD User service (paper app:protocol)
// the legacy name "researcher" survives in the CLI subcommand and in the
// metric phase prefixes so historical CSVs and SLURM scripts keep working
package user

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
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
	DataDir string
	Backend crypto.Backend
	Log     *zap.Logger
}

type Server struct {
	cfg Config
	m   *metrics.Writer
	r   *chi.Mux
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0755); err != nil {
		cfg.Log.Warn("could not create data dir", zap.Error(err))
	}
	s := &Server{cfg: cfg, m: metrics.NewWriter(cfg.DataDir)}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Post("/generate-key", s.handleGenerateKey)
	s.r.Post("/install-sk", s.handleInstallSK)
	s.r.Get("/get-secret-key", s.handleGetSecretKey)
	s.r.Post("/decrypt", s.handleDecrypt)
	s.r.Post("/encrypt-query", s.handleEncryptQuery)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleGenerateKey(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID string `json:"run_id"`
	}
	json.NewDecoder(r.Body).Decode(&req)

	t := time.Now()
	if err := s.cfg.Backend.GenerateResearcherKey(s.cfg.DataDir); err != nil {
		s.cfg.Log.Error("GenerateResearcherKey failed", zap.String("entity", "researcher"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "success"
	s.m.Record("researcher-keygen", time.Since(t), meta, 0, 0)
	s.cfg.Log.Info("key generated", zap.String("entity", "researcher"), zap.String("run_id", req.RunID))
	w.WriteHeader(http.StatusOK)
}

// install SK from the Key Manager (paper §3.3)
// replaces handleGenerateKey: the user only ever holds a KM-distributed SK
func (s *Server) handleInstallSK(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID string `json:"run_id"`
		KMURL string `json:"km_url"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.KMURL == "" {
		http.Error(w, "km_url required", http.StatusBadRequest)
		return
	}

	t := time.Now()
	resp, err := http.Get(req.KMURL + "/sk/researcher")
	if err != nil {
		http.Error(w, "fetch KM: "+err.Error(), http.StatusBadGateway)
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		http.Error(w, fmt.Sprintf("KM status %d: %s", resp.StatusCode, body),
			http.StatusBadGateway)
		return
	}
	n, err := keymanager.ExtractTar(resp.Body, s.cfg.DataDir)
	if err != nil {
		http.Error(w, "extract: "+err.Error(), http.StatusInternalServerError)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "from-km"
	s.m.Record("researcher-install-sk", time.Since(t), meta, 0, n)
	s.cfg.Log.Info("researcher SK installed from KM",
		zap.String("entity", "researcher"),
		zap.String("run_id", req.RunID), zap.Int64("bytes", n))
	w.WriteHeader(http.StatusOK)
}

// serve secret_key.bin for KSK derivation
func (s *Server) handleGetSecretKey(w http.ResponseWriter, _ *http.Request) {
	data, err := os.ReadFile(s.cfg.DataDir + "/secret_key.bin")
	if err != nil {
		http.Error(w, "secret key not found", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", `attachment; filename="secret_key.bin"`)
	w.Write(data)
}

// encrypt query literals under the user key
// the proxy key-switches them into the provider domain before forwarding
func (s *Server) handleEncryptQuery(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID        string  `json:"run_id"`
		Instructions string  `json:"instructions"`
		Values       []int64 `json:"values"` // distinct literal values to encrypt
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	outPath := s.cfg.DataDir + "/literals.bin"
	t := time.Now()
	if err := s.cfg.Backend.EncryptLiterals(s.cfg.DataDir, req.Values, outPath); err != nil {
		s.cfg.Log.Error("EncryptLiterals failed", zap.String("entity", "researcher"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	data, err := os.ReadFile(outPath)
	if err != nil {
		http.Error(w, "read literals: "+err.Error(), http.StatusInternalServerError)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "success"
	s.m.Record("researcher-encrypt-query", time.Since(t), meta, int64(len(data)), 0)
	s.cfg.Log.Info("query literals encrypted", zap.String("entity", "researcher"), zap.String("run_id", req.RunID), zap.Int("literals", len(req.Values)))
	// raw bytes, not hex-in-JSON: that doubled every literal transfer
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Write(data)
}

// decrypt a result ciphertext into row indicators
func (s *Server) handleDecrypt(w http.ResponseWriter, r *http.Request) {
	rows, _ := strconv.Atoi(r.URL.Query().Get("rows"))
	if rows <= 0 {
		rows = 1000
	}
	runID := r.URL.Query().Get("run_id")
	if runID == "" {
		runID = "no_id"
	}

	data, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "read body", http.StatusBadRequest)
		return
	}

	resultPath := s.cfg.DataDir + "/uploaded_result.bin"
	outputPath := s.cfg.DataDir + "/decrypted_result.json"
	if err := os.WriteFile(resultPath, data, 0o600); err != nil {
		http.Error(w, "write temp", http.StatusInternalServerError)
		return
	}
	defer os.Remove(resultPath)

	t := time.Now()
	if err := s.cfg.Backend.DecryptResult(s.cfg.DataDir, resultPath, outputPath, rows); err != nil {
		s.cfg.Log.Error("DecryptResult failed", zap.String("entity", "researcher"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	dur := time.Since(t)
	meta := metrics.ParseRunID(runID)
	meta.Status = "success"
	s.m.Record("researcher-decrypt", dur, meta, 0, int64(len(data)))
	s.cfg.Log.Info("result decrypted", zap.String("entity", "researcher"), zap.String("run_id", runID), zap.Int("rows", rows), zap.Duration("dur", dur))

	out, err := os.ReadFile(outputPath)
	if err != nil {
		http.Error(w, "read output", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Write(out)
}
