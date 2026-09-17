//go:build cgo_backends

package cmd

import (
	"github.com/fecad/internal/crypto"
	"github.com/fecad/internal/crypto/engorgio"
	"github.com/fecad/internal/crypto/he3db"
	"github.com/fecad/internal/crypto/patdiscover"
)

// engines behind the C++ bridges
// -tags cgo_backends only: linking needs OpenFHE, TFHE++, NTL and GMP
func cgoBackend(name, buildDir string) crypto.Backend {
	switch name {
	case "he3db":
		return he3db.New(buildDir)
	case "engorgio":
		return engorgio.New(buildDir)
	case "patdiscover":
		return patdiscover.New(buildDir)
	}
	return nil
}
