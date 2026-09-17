package board

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"go.uber.org/zap"

	"github.com/fecad/internal/blssig"
)

// one auto-approve rule
type TierAPolicy struct {
	Class   string `json:"class"`
	Purpose string `json:"purpose"`
}

// server parameters
type Config struct {
	DataDir       string
	TierAPolicies []TierAPolicy // loaded from policies.json; nil = approve all
	Log           *zap.Logger
}

type Server struct {
	cfg      Config
	r        *chi.Mux
	mu       sync.Mutex
	sk       []byte                  // BLS12-381 private scalar (32 bytes)
	pk       []byte                  // BLS12-381 G2 public key (96 bytes)
	issued   map[string]string       // qd -> sig_rev_hex
	pendingB map[string]*reviewEntry // qd -> entry (Tier B queue)
}

type reviewEntry struct {
	QCommitment string `json:"ct_commitment"` // SHA-256(Q ‖ r_seed)
	QD          string `json:"qd"`
	SigRHex     string `json:"sig_r_hex"`
	PKRHex      string `json:"pk_r_hex"`
	CreatedAt   int64  `json:"created_at"`
}

func New(cfg Config) *Server {
	if err := os.MkdirAll(cfg.DataDir, 0o755); err != nil {
		cfg.Log.Warn("mkdir data dir", zap.Error(err))
	}
	s := &Server{
		cfg:      cfg,
		issued:   make(map[string]string),
		pendingB: make(map[string]*reviewEntry),
	}
	if err := s.loadOrGenKeyPair(); err != nil {
		cfg.Log.Error("board keypair init", zap.Error(err))
	}
	s.r = chi.NewRouter()
	s.r.Use(middleware.Recoverer)
	s.r.Get("/health", s.handleHealth)
	s.r.Get("/pk", s.handlePK)
	s.r.Post("/board/review", s.handleReview)
	s.r.Get("/review/pending", s.handlePending)
	s.r.Post("/review/decide", s.handleDecide)
	s.r.Get("/sig-rev", s.handleSigRev)
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) { s.r.ServeHTTP(w, r) }

func (s *Server) loadOrGenKeyPair() error {
	skPath := s.cfg.DataDir + "/board_sk.bin"
	pkPath := s.cfg.DataDir + "/board_pk.bin"
	if skData, err := os.ReadFile(skPath); err == nil && len(skData) == blssig.PrivateKeySize {
		if pkData, err2 := os.ReadFile(pkPath); err2 == nil && len(pkData) == blssig.PublicKeySize {
			s.sk = skData
			s.pk = pkData
			return nil
		}
	}
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
	s.sk = sk
	s.pk = pk
	return nil
}

func (s *Server) handleHealth(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	n := len(s.issued)
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{"status": "ok", "issued": n})
}

func (s *Server) handlePK(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"pk_rev_hex": hex.EncodeToString(s.pk)})
}

// POST /board/review body
type ReviewRequest struct {
	QJSON        string `json:"q_json"`        // plaintext query as JSON string
	RSeedHex     string `json:"r_seed_hex"`    // randomness seed
	CTCommitment string `json:"ct_commitment"` // SHA-256(q_json ‖ r_seed_hex)
	QD           string `json:"qd"`
	SigRHex      string `json:"sig_r_hex"` // researcher's sig over H(q_json ‖ ct_commitment ‖ qd)
	PKRHex       string `json:"pk_r_hex"`  // researcher's public key
}

func (s *Server) handleReview(w http.ResponseWriter, r *http.Request) {
	var req ReviewRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}

	// 1. verify sig_R
	pkR, err := hex.DecodeString(req.PKRHex)
	if err != nil || len(pkR) != blssig.PublicKeySize {
		http.Error(w, "invalid pk_r_hex", http.StatusBadRequest)
		return
	}
	sigR, _ := hex.DecodeString(req.SigRHex)
	msg := sha256.Sum256([]byte(req.QJSON + req.CTCommitment + req.QD))
	if !blssig.Verify(pkR, msg[:], sigR) {
		http.Error(w, "sig_R verification failed", http.StatusForbidden)
		return
	}

	// 2. binding check
	expected := sha256.Sum256([]byte(req.QJSON + req.RSeedHex))
	if hex.EncodeToString(expected[:]) != req.CTCommitment {
		http.Error(w, "binding check failed", http.StatusForbidden)
		return
	}

	// 3. tier-A policy
	if s.tierAPass(req.QD, req.QJSON) {
		sigRev := s.signAdmission(req.QD, req.CTCommitment)
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"status":      "approved",
			"tier":        "A",
			"sig_rev_hex": sigRev,
		})
		return
	}

	// 4. queue for tier-B review
	s.mu.Lock()
	if _, already := s.issued[req.QD]; !already {
		s.pendingB[req.QD] = &reviewEntry{
			QCommitment: req.CTCommitment,
			QD:          req.QD,
			SigRHex:     req.SigRHex,
			PKRHex:      req.PKRHex,
			CreatedAt:   time.Now().Unix(),
		}
	}
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "pending_review", "qd": req.QD, "tier": "B"})
}

// tier-A admission check
// an empty policy list auto-approves everything
func (s *Server) tierAPass(qd, qJSON string) bool {
	if len(s.cfg.TierAPolicies) == 0 {
		return true
	}
	var q struct {
		Class   string `json:"class"`
		Purpose string `json:"purpose"`
	}
	json.Unmarshal([]byte(qJSON), &q)
	for _, pol := range s.cfg.TierAPolicies {
		if q.Class == pol.Class && q.Purpose == pol.Purpose {
			return true
		}
	}
	return false
}

func (s *Server) signAdmission(qd, ctCommitment string) string {
	msg := sha256.Sum256([]byte(qd + ctCommitment))
	sig, err := blssig.Sign(s.sk, msg[:])
	if err != nil {
		s.cfg.Log.Error("board BLS sign failed", zap.Error(err))
		return ""
	}
	sigHex := hex.EncodeToString(sig)
	s.mu.Lock()
	s.issued[qd] = sigHex
	delete(s.pendingB, qd) // erase post-review (§ operational erasure)
	s.mu.Unlock()
	return sigHex
}

func (s *Server) handlePending(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	entries := make([]*reviewEntry, 0, len(s.pendingB))
	for _, e := range s.pendingB {
		entries = append(entries, e)
	}
	s.mu.Unlock()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]any{"pending": entries})
}

type decideRequest struct {
	QD      string `json:"qd"`
	Approve bool   `json:"approve"`
}

func (s *Server) handleDecide(w http.ResponseWriter, r *http.Request) {
	var req decideRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad JSON", http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	entry, ok := s.pendingB[req.QD]
	s.mu.Unlock()
	if !ok {
		http.Error(w, "qd not in Tier-B queue", http.StatusNotFound)
		return
	}
	if !req.Approve {
		s.mu.Lock()
		delete(s.pendingB, req.QD)
		s.mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{"status": "rejected", "qd": req.QD})
		return
	}
	sigRev := s.signAdmission(req.QD, entry.QCommitment)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "approved", "sig_rev_hex": sigRev, "qd": req.QD})
}

func (s *Server) handleSigRev(w http.ResponseWriter, r *http.Request) {
	qd := r.URL.Query().Get("qd")
	s.mu.Lock()
	sig, ok := s.issued[qd]
	s.mu.Unlock()
	if !ok {
		http.Error(w, "no token for qd", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"sig_rev_hex": sig,
		"pk_rev_hex":  hex.EncodeToString(s.pk),
	})
}
