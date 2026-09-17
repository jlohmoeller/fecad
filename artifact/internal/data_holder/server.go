// Package data_holder implements the data-holder half of the Provider role
// (paper app:protocol steps A and C): custody of sk_h, encryption of the
// records under it, and the per-patient blinding ciphertexts; only the
// evaluation key ever reaches the evaluator
package data_holder

import (
	"archive/tar"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
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

type Server struct {
	cfg Config
	m   *metrics.Writer
	r   *chi.Mux
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0755); err != nil {
		cfg.Log.Warn("could not create data dir", zap.Error(err))
	}
	s := &Server{
		cfg: cfg,
		m:   metrics.NewWriter(cfg.DataDir),
	}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Post("/generate-keys", s.handleGenerateKeys)
	s.r.Post("/install-keys", s.handleInstallKeys)
	s.r.Post("/encrypt-data", s.handleEncryptData)
	s.r.Get("/get-secret-key", s.handleGetSecretKey)
	s.r.Post("/enroll-patient", s.handleEnrollPatient)
	s.r.Get("/store-tar", s.handleStoreTar)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.WriteHeader(http.StatusOK)
}

// keygen + encrypt for bench run-task
func (s *Server) SetupForBench(runID string) error {
	if err := s.cfg.Backend.GenerateProviderKeys(s.cfg.DataDir); err != nil {
		return fmt.Errorf("GenerateProviderKeys: %w", err)
	}
	if err := s.cfg.Backend.EncryptData(s.cfg.DataDir, s.cfg.SchemaPath); err != nil {
		return fmt.Errorf("EncryptData: %w", err)
	}
	return nil
}

func (s *Server) handleGenerateKeys(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID string `json:"run_id"`
	}
	json.NewDecoder(r.Body).Decode(&req)

	t := time.Now()
	if err := s.cfg.Backend.GenerateProviderKeys(s.cfg.DataDir); err != nil {
		s.cfg.Log.Error("GenerateProviderKeys failed", zap.String("entity", "provider"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "success"
	s.m.Record("provider-keygen", time.Since(t), meta, 0, 0)
	s.cfg.Log.Info("keys generated", zap.String("entity", "provider"), zap.String("run_id", req.RunID))
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleInstallKeys(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID      string `json:"run_id"`
		KMURL      string `json:"km_url"`
		ProviderID int    `json:"provider_id"`
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
	url := fmt.Sprintf("%s/sk/provider?id=%d", req.KMURL, req.ProviderID)
	resp, err := http.Get(url)
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
	meta.Status = fmt.Sprintf("provider_%d", req.ProviderID)
	s.m.Record("provider-install-keys", time.Since(t), meta, 0, n)
	s.cfg.Log.Info("provider keys installed from KM",
		zap.String("entity", "provider"),
		zap.String("run_id", req.RunID),
		zap.Int("provider_id", req.ProviderID),
		zap.Int64("bytes", n))
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleEncryptData(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID string `json:"run_id"`
	}
	json.NewDecoder(r.Body).Decode(&req)

	t := time.Now()
	if err := s.cfg.Backend.EncryptData(s.cfg.DataDir, s.cfg.SchemaPath); err != nil {
		s.cfg.Log.Error("EncryptData failed", zap.String("entity", "provider"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	info, _ := os.Stat(s.cfg.DataDir + "/encrypted_data.bin")
	var nbytes int64
	if info != nil {
		nbytes = info.Size()
	}
	meta := metrics.ParseRunID(req.RunID)
	meta.Status = "success"
	// encrypted_data.bin never goes on the wire, so it is recorded as an
	// artifact size and stays out of the comm plot
	s.m.RecordSized("provider-encrypt", time.Since(t), meta, 0, 0, nbytes)
	s.cfg.Log.Info("data encrypted", zap.String("entity", "provider"), zap.String("run_id", req.RunID), zap.Int64("bytes", nbytes))
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleGetSecretKey(w http.ResponseWriter, _ *http.Request) {
	data, err := os.ReadFile(s.cfg.DataDir + "/secret_key.bin")
	if err != nil {
		http.Error(w, "secret key not found", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Write(data)
}

// generate per-patient blinding, return the cancellation CT
// the −r_p ciphertext is the patient's envelope material (paper
// app:consent-envelope), released once here and cached at the proxy
// across queries (paper §5)
func (s *Server) handleEnrollPatient(w http.ResponseWriter, r *http.Request) {
	t := time.Now()
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "read body: "+err.Error(), http.StatusBadRequest)
		return
	}
	var req struct {
		PatientID string `json:"patient_id"`
		PKHex     string `json:"pk_hex"`
		RecordIDs []int  `json:"record_ids"`
	}
	if err := json.Unmarshal(raw, &req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	tmpDir, err := os.MkdirTemp("", "cancel-*")
	if err != nil {
		http.Error(w, "mktemp: "+err.Error(), http.StatusInternalServerError)
		return
	}
	defer os.RemoveAll(tmpDir)

	if err := s.cfg.Backend.SetupPatientBlinding(
		s.cfg.DataDir, tmpDir, req.PatientID, req.RecordIDs,
	); err != nil {
		s.cfg.Log.Error("SetupPatientBlinding failed",
			zap.String("entity", "provider"),
			zap.String("patient", req.PatientID), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	ctPath := fmt.Sprintf("%s/cancellations/%s.bin", tmpDir, req.PatientID)
	ctData, err := os.ReadFile(ctPath)
	if err != nil {
		http.Error(w, "read cancellation CT: "+err.Error(), http.StatusInternalServerError)
		return
	}

	s.cfg.Log.Info("patient enrolled",
		zap.String("entity", "provider"),
		zap.String("patient_id", req.PatientID),
		zap.Int("records", len(req.RecordIDs)))

	respBody, _ := json.Marshal(map[string]string{
		"cancellation_ct_hex": hex.EncodeToString(ctData),
	})
	w.Header().Set("Content-Type", "application/json")
	w.Write(respBody)

	// mirrors patient-enroll from the data-holder side; the artifact size is
	// the raw cancellation CT on disk, not its hex form
	meta := metrics.Meta{Status: req.PatientID}
	s.m.RecordSized("provider-enroll-patient", time.Since(t), meta,
		int64(len(respBody)), int64(len(raw)), int64(len(ctData)))
}

// never leaves the data holder: sk_h and the plaintext records
// the metrics file is excluded too, so it cannot clobber the evaluator's
// own performance_metrics.csv on extraction
var storeExcluded = map[string]bool{
	"secret_key.bin":          true,
	"data.json":               true,
	"performance_metrics.csv": true,
}

// stream the evaluator's share of the store (paper app:protocol step C)
// everything except storeExcluded ships, so the role boundary is defined by
// what stays behind; only the storage-disjoint deployment
// (bench --split-storage) ever calls this
func (s *Server) handleStoreTar(w http.ResponseWriter, r *http.Request) {
	t := time.Now()
	w.Header().Set("Content-Type", "application/x-tar")
	cw := &countingWriter{w: w}
	if err := tarStore(cw, s.cfg.DataDir); err != nil {
		// the header is already sent, so drop the connection: the puller must
		// fail on a truncated tar rather than install a partial store
		s.cfg.Log.Error("store tar failed",
			zap.String("entity", "provider"), zap.Error(err))
		return
	}
	meta := metrics.ParseRunID(r.URL.Query().Get("run_id"))
	meta.Status = "success"
	s.m.Record("provider-store-upload", time.Since(t), meta, cw.n, 0)
	s.cfg.Log.Info("store shipped to evaluator",
		zap.String("entity", "provider"), zap.Int64("bytes", cw.n))
}

type countingWriter struct {
	w io.Writer
	n int64
}

func (c *countingWriter) Write(p []byte) (int, error) {
	n, err := c.w.Write(p)
	c.n += int64(n)
	return n, err
}

// tar dir recursively, minus storeExcluded
func tarStore(w io.Writer, dir string) error {
	tw := tar.NewWriter(w)
	defer tw.Close()

	return filepath.WalkDir(dir, func(path string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		rel, err := filepath.Rel(dir, path)
		if err != nil {
			return err
		}
		if storeExcluded[rel] {
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		hdr := &tar.Header{
			Name:    filepath.ToSlash(rel),
			Mode:    0o600,
			Size:    info.Size(),
			ModTime: info.ModTime(),
		}
		if err := tw.WriteHeader(hdr); err != nil {
			return err
		}
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		defer f.Close()
		_, err = io.Copy(tw, f)
		return err
	})
}
