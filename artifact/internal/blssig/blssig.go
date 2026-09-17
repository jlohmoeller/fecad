// Package blssig wraps circl BLS12-381 (paper §3.3)
package blssig

import (
	"crypto/rand"
	"errors"
	"fmt"

	"github.com/cloudflare/circl/sign/bls"
)

const (
	PrivateKeySize = 32
	PublicKeySize  = 96
	SignatureSize  = 48
)

// fresh keypair, marshalled
func GenerateKey() (sk, pk []byte, err error) {
	ikm := make([]byte, 32)
	if _, err := rand.Read(ikm); err != nil {
		return nil, nil, fmt.Errorf("blssig: read ikm: %w", err)
	}
	priv, err := bls.KeyGen[bls.G2](ikm, nil, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("blssig: keygen: %w", err)
	}
	skBytes, err := priv.MarshalBinary()
	if err != nil {
		return nil, nil, fmt.Errorf("blssig: marshal sk: %w", err)
	}
	pkBytes, err := priv.PublicKey().MarshalBinary()
	if err != nil {
		return nil, nil, fmt.Errorf("blssig: marshal pk: %w", err)
	}
	return skBytes, pkBytes, nil
}

// G1 signature over msg
func Sign(sk, msg []byte) ([]byte, error) {
	priv := new(bls.PrivateKey[bls.G2])
	if err := priv.UnmarshalBinary(sk); err != nil {
		return nil, fmt.Errorf("blssig: bad sk: %w", err)
	}
	return []byte(bls.Sign(priv, msg)), nil
}

func Verify(pk, msg, sig []byte) bool {
	pub := new(bls.PublicKey[bls.G2])
	if err := pub.UnmarshalBinary(pk); err != nil {
		return false
	}
	return bls.Verify(pub, msg, bls.Signature(sig))
}

// fold signatures into one G1 signature
// does not verify the inputs
func Aggregate(sigs [][]byte) ([]byte, error) {
	if len(sigs) == 0 {
		return nil, errors.New("blssig: empty signature list")
	}
	in := make([]bls.Signature, len(sigs))
	for i, s := range sigs {
		in[i] = bls.Signature(s)
	}
	agg, err := bls.Aggregate(bls.G2{}, in)
	if err != nil {
		return nil, fmt.Errorf("blssig: aggregate: %w", err)
	}
	return []byte(agg), nil
}

// check aggregate against (pk, msg) pairs
func VerifyAggregate(pks [][]byte, msgs [][]byte, aggSig []byte) bool {
	if len(pks) != len(msgs) || len(pks) == 0 {
		return false
	}
	pubs := make([]*bls.PublicKey[bls.G2], len(pks))
	for i, p := range pks {
		pubs[i] = new(bls.PublicKey[bls.G2])
		if err := pubs[i].UnmarshalBinary(p); err != nil {
			return false
		}
	}
	return bls.VerifyAggregate(pubs, msgs, bls.Signature(aggSig))
}
