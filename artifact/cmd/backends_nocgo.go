//go:build !cgo_backends

package cmd

import "github.com/fecad/internal/crypto"

// stub without the C++ bridges
func cgoBackend(name, buildDir string) crypto.Backend { return nil }
