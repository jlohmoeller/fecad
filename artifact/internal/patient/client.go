package patient

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"

	"github.com/fecad/internal/blssig"
)

// in-process patient handle (paper §A.1)
// lets orchestrators drive consent without an HTTP server per patient
type Client struct {
	PatientID         string
	SK                []byte
	PK                []byte
	RecordIDs         []int
	CancellationCTHex string // set at registration; bound by EnvelopeSign
}

// new patient, fresh BLS keypair
func NewClient(patientID string, recordIDs []int) (*Client, error) {
	sk, pk, err := blssig.GenerateKey()
	if err != nil {
		return nil, fmt.Errorf("patient.NewClient: %w", err)
	}
	return &Client{
		PatientID: patientID,
		SK:        sk,
		PK:        pk,
		RecordIDs: recordIDs,
	}, nil
}

// per-query consent signature (paper app:consent-envelope)
//
//	sig_p = BLS.Sign(sk_p, H(qd ‖ ct_C))
//
// binding to ct_C rather than qd alone stops a proxy replaying consent
// against an unrelated cancellation ciphertext
//
// DEFERRED (paper §5): only σ_p ships per query, reusing the proxy-cached
// enrollment ct_C; the fresh-ct_C path is gated on ct_C size (~116 MiB
// for CKKS/Engorgio)
func (c *Client) EnvelopeSign(qd string) ([]byte, error) {
	qdBytes, err := hex.DecodeString(qd)
	if err != nil {
		return nil, fmt.Errorf("EnvelopeSign: bad qd hex: %w", err)
	}
	ctBytes, err := hex.DecodeString(c.CancellationCTHex)
	if err != nil {
		return nil, fmt.Errorf("EnvelopeSign: bad cancellation hex: %w", err)
	}
	h := sha256.New()
	h.Write(qdBytes)
	h.Write(ctBytes)
	return blssig.Sign(c.SK, h.Sum(nil))
}

// bytes that EnvelopeSign signs
func EnvelopeMessage(qd, cancellationCTHex string) ([]byte, error) {
	qdBytes, err := hex.DecodeString(qd)
	if err != nil {
		return nil, err
	}
	ctBytes, err := hex.DecodeString(cancellationCTHex)
	if err != nil {
		return nil, err
	}
	h := sha256.New()
	h.Write(qdBytes)
	h.Write(ctBytes)
	return h.Sum(nil), nil
}
