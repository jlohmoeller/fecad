// Package keymanager implements the FeCaD Key Manager (paper §3.3)
// the only entity that ever holds the researcher and provider secret keys
// together, long enough to derive the two key-switching keys the proxy needs;
// it can be taken offline afterwards and is never on a per-query path
//
// provider material is derived on demand by /setup-provider and released by
// /cleanup-provider, which bounds peak KM disk to one provider's keys instead
// of N — hundreds of MB each for the backends with large eval-mult material
package keymanager

import (
	"archive/tar"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"go.uber.org/zap"

	"github.com/fecad/internal/crypto"
	"github.com/fecad/internal/metrics"
)

// server parameters
type Config struct {
	DataDir string
	Backend crypto.Backend
	Log     *zap.Logger
}

type Server struct {
	cfg          Config
	m            *metrics.Writer
	r            *chi.Mux
	mu           sync.Mutex
	numProviders int
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0o755); err != nil {
		cfg.Log.Warn("mkdir km data dir", zap.Error(err))
	}
	s := &Server{cfg: cfg, m: metrics.NewWriter(cfg.DataDir)}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Post("/setup", s.handleSetup)
	s.r.Post("/setup-provider", s.handleSetupProvider)
	s.r.Post("/cleanup-provider", s.handleCleanupProvider)
	s.r.Get("/sk/researcher", s.handleResearcherSK)
	s.r.Get("/sk/provider", s.handleProviderSK)
	s.r.Get("/ksk", s.handleKSK)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	w.WriteHeader(http.StatusOK)
}

// KM-side researcher key directory
func ResearcherDir(km string) string { return filepath.Join(km, "researcher") }

// KM-side provider key directory
func ProviderDir(km string, id int) string {
	return filepath.Join(km, fmt.Sprintf("provider_%d", id))
}

// KSK path, direction "LtoR" or "RtoL"
func KSKPath(km string, id int, direction string) string {
	return filepath.Join(km, fmt.Sprintf("ksk_%s_provider_%d.bin", direction, id))
}

// derive the researcher key only
// provider keys are deferred to /setup-provider so per-provider material
// never accumulates on KM disk
func (s *Server) handleSetup(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID        string `json:"run_id"`
		NumProviders int    `json:"num_providers"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.NumProviders <= 0 {
		http.Error(w, "num_providers must be > 0", http.StatusBadRequest)
		return
	}
	base := metrics.ParseRunID(req.RunID)

	tTotal := time.Now()

	rDir := ResearcherDir(s.cfg.DataDir)
	if err := os.MkdirAll(rDir, 0o755); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	tR := time.Now()
	if err := s.cfg.Backend.GenerateResearcherKey(rDir); err != nil {
		s.cfg.Log.Error("KM researcher keygen failed",
			zap.String("entity", "keymanager"), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	rMeta := base
	rMeta.Status = "researcher"
	s.m.Record("km-keygen-researcher", time.Since(tR), rMeta, 0, 0)

	s.mu.Lock()
	s.numProviders = req.NumProviders
	s.mu.Unlock()

	totalMeta := base
	totalMeta.Status = fmt.Sprintf("providers=%d", req.NumProviders)
	s.m.Record("km-setup-total", time.Since(tTotal), totalMeta, 0, 0)

	s.cfg.Log.Info("KM setup complete (researcher only)",
		zap.String("entity", "keymanager"),
		zap.String("run_id", req.RunID),
		zap.Int("providers", req.NumProviders),
		zap.Duration("dur", time.Since(tTotal)))

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{
		"ok":            true,
		"num_providers": req.NumProviders,
	})
}

// derive one provider's SK and both KSKs
// pair with /cleanup-provider once the entities have downloaded the material
func (s *Server) handleSetupProvider(w http.ResponseWriter, r *http.Request) {
	var req struct {
		RunID      string `json:"run_id"`
		ProviderID int    `json:"provider_id"`
		// slot offset in the federation-wide indicator vector; a non-zero
		// value folds the matching rotation into ksk_{L->R} so the proxy can
		// sum providers into one ct_R
		RowOffset int `json:"row_offset"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.ProviderID < 0 {
		http.Error(w, "provider_id must be >= 0", http.StatusBadRequest)
		return
	}
	base := metrics.ParseRunID(req.RunID)
	pMeta := base
	pMeta.Status = fmt.Sprintf("provider_%d", req.ProviderID)

	rDir := ResearcherDir(s.cfg.DataDir)
	pDir := ProviderDir(s.cfg.DataDir, req.ProviderID)
	if err := os.MkdirAll(pDir, 0o755); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	tP := time.Now()
	if err := s.cfg.Backend.GenerateProviderKeys(pDir); err != nil {
		s.cfg.Log.Error("KM provider keygen failed",
			zap.String("entity", "keymanager"),
			zap.Int("provider", req.ProviderID), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	s.m.Record("km-keygen-provider", time.Since(tP), pMeta, 0, 0)

	tLR := time.Now()
	genKSK := func() error {
		if oa, ok := s.cfg.Backend.(crypto.OffsetAggregator); ok && req.RowOffset != 0 {
			return oa.GenerateKSKAtOffset(
				rDir, pDir, KSKPath(s.cfg.DataDir, req.ProviderID, "LtoR"), req.RowOffset)
		}
		return s.cfg.Backend.GenerateKSK(
			rDir, pDir, KSKPath(s.cfg.DataDir, req.ProviderID, "LtoR"))
	}
	if err := genKSK(); err != nil {
		s.cfg.Log.Error("KM ksk_L->R derivation failed",
			zap.String("entity", "keymanager"),
			zap.Int("provider", req.ProviderID), zap.Error(err))
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	s.m.Record("km-ksk-LtoR", time.Since(tLR), pMeta, 0, 0)

	// ksk_R->L is only needed by the encrypted-literals path (paper §4.2),
	// so a backend without it must not abort setup
	tRL := time.Now()
	if err := s.cfg.Backend.GenerateKSKQueryForward(
		rDir, pDir, KSKPath(s.cfg.DataDir, req.ProviderID, "RtoL"),
	); err != nil {
		s.cfg.Log.Warn("KM ksk_R->L derivation failed; skipping",
			zap.String("entity", "keymanager"),
			zap.Int("provider", req.ProviderID), zap.Error(err))
	} else {
		s.m.Record("km-ksk-RtoL", time.Since(tRL), pMeta, 0, 0)
	}

	w.WriteHeader(http.StatusOK)
}

// delete one provider's key directory and KSKs
// cleaning eagerly is what bounds KM disk to one provider regardless of N
func (s *Server) handleCleanupProvider(w http.ResponseWriter, r *http.Request) {
	var req struct {
		ProviderID int `json:"provider_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	if req.ProviderID < 0 {
		http.Error(w, "provider_id must be >= 0", http.StatusBadRequest)
		return
	}
	paths := []string{
		ProviderDir(s.cfg.DataDir, req.ProviderID),
		KSKPath(s.cfg.DataDir, req.ProviderID, "LtoR"),
		KSKPath(s.cfg.DataDir, req.ProviderID, "RtoL"),
	}
	for _, p := range paths {
		if err := os.RemoveAll(p); err != nil {
			s.cfg.Log.Warn("KM cleanup-provider remove failed",
				zap.String("entity", "keymanager"),
				zap.Int("provider", req.ProviderID),
				zap.String("path", p), zap.Error(err))
		}
	}
	w.WriteHeader(http.StatusOK)
}

// tally bytes without buffering the payload
type countingWriter struct {
	w io.Writer
	n int64
}

func (c *countingWriter) Write(p []byte) (int, error) {
	n, err := c.w.Write(p)
	c.n += int64(n)
	return n, err
}

// tar of the researcher key directory
// a tar rather than one file because some backends (engorgio) produce
// several files per entity that must all reach the receiver; the recorded
// net_out matches the researcher-install-sk net_in on the user side
func (s *Server) handleResearcherSK(w http.ResponseWriter, _ *http.Request) {
	t := time.Now()
	w.Header().Set("Content-Type", "application/x-tar")
	cw := &countingWriter{w: w}
	if err := tarDir(cw, ResearcherDir(s.cfg.DataDir)); err != nil {
		s.cfg.Log.Error("tar researcher dir failed",
			zap.String("entity", "keymanager"), zap.Error(err))
	}
	s.m.Record("km-send-researcher-sk", time.Since(t),
		metrics.Meta{Status: "researcher"}, cw.n, 0)
}

// tar of one provider's key directory
// the recorded net_out matches provider-install-keys net_in
func (s *Server) handleProviderSK(w http.ResponseWriter, r *http.Request) {
	id, err := paramInt(r, "id")
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	t := time.Now()
	w.Header().Set("Content-Type", "application/x-tar")
	cw := &countingWriter{w: w}
	if err := tarDir(cw, ProviderDir(s.cfg.DataDir, id)); err != nil {
		s.cfg.Log.Error("tar provider dir failed",
			zap.String("entity", "keymanager"),
			zap.Int("provider", id), zap.Error(err))
	}
	s.m.Record("km-send-provider-sk", time.Since(t),
		metrics.Meta{Status: fmt.Sprintf("provider_%d", id)}, cw.n, 0)
}

// one KSK file, one direction per request
// the recorded net_out matches proxy-install-ksk-{direction} net_in
func (s *Server) handleKSK(w http.ResponseWriter, r *http.Request) {
	id, err := paramInt(r, "provider_id")
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	direction := r.URL.Query().Get("direction")
	if direction != "LtoR" && direction != "RtoL" {
		http.Error(w, "direction must be LtoR or RtoL", http.StatusBadRequest)
		return
	}
	path := KSKPath(s.cfg.DataDir, id, direction)
	data, err := os.ReadFile(path)
	if err != nil {
		http.Error(w, "not found: "+err.Error(), http.StatusNotFound)
		return
	}
	t := time.Now()
	w.Header().Set("Content-Type", "application/octet-stream")
	n, _ := w.Write(data)
	s.m.Record("km-send-ksk-"+direction, time.Since(t),
		metrics.Meta{Status: fmt.Sprintf("provider_%d", id)}, int64(n), 0)
}

// tar of dir, top-level files only
// key material is flat, and not recursing avoids leaking subdirectories
func tarDir(w io.Writer, dir string) error {
	tw := tar.NewWriter(w)
	defer tw.Close()

	entries, err := os.ReadDir(dir)
	if err != nil {
		return err
	}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		info, err := e.Info()
		if err != nil {
			return err
		}
		hdr := &tar.Header{
			Name:    e.Name(),
			Mode:    0o600,
			Size:    info.Size(),
			ModTime: info.ModTime(),
		}
		if err := tw.WriteHeader(hdr); err != nil {
			return err
		}
		f, err := os.Open(filepath.Join(dir, e.Name()))
		if err != nil {
			return err
		}
		if _, err := io.Copy(tw, f); err != nil {
			f.Close()
			return err
		}
		f.Close()
	}
	return nil
}

// extract a tar stream into dir, returning bytes written
func ExtractTar(r io.Reader, dir string) (int64, error) {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return 0, err
	}
	tr := tar.NewReader(r)
	var total int64
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			return total, nil
		}
		if err != nil {
			return total, err
		}
		if hdr.Typeflag != tar.TypeReg {
			continue
		}
		// reject path traversal
		clean := filepath.Clean(hdr.Name)
		if filepath.IsAbs(clean) || clean == ".." ||
			len(clean) >= 3 && clean[0:3] == "../" {
			return total, fmt.Errorf("unsafe tar entry: %s", hdr.Name)
		}
		// nested entries (the data holder's blindings/ subtree) need their
		// parent created first
		if parent := filepath.Dir(clean); parent != "." {
			if err := os.MkdirAll(filepath.Join(dir, parent), 0o755); err != nil {
				return total, err
			}
		}
		out, err := os.OpenFile(
			filepath.Join(dir, clean),
			os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o600,
		)
		if err != nil {
			return total, err
		}
		n, err := io.Copy(out, tr)
		out.Close()
		total += n
		if err != nil {
			return total, err
		}
	}
}

func paramInt(r *http.Request, name string) (int, error) {
	v := r.URL.Query().Get(name)
	if v == "" {
		return 0, fmt.Errorf("missing %s", name)
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return 0, fmt.Errorf("bad %s: %w", name, err)
	}
	return n, nil
}
