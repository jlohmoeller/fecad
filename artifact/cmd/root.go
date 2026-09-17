// FeCaD CLI examples
//
//	fecad researcher serve --addr :8000 --data-dir ./data/researcher
//	fecad proxy serve --addr :8002 --data-dir ./data/proxy
//	fecad provider serve --addr :10000 --data-dir ./data/loc_0 --schema ./schemas/nuclear_medicine.json
//	fecad patient serve --addr :9000
//	fecad bench run-task --task-id 0 --dataset nuclear_medicine --query q1 --records 1000
//	fecad bench run --dataset nuclear_medicine --query q1 --records 1000 --runs 30
//	fecad bench bulk
//	fecad data generate --schema ./schemas/nuclear_medicine.json --count 1000 --out data.json
package cmd

import (
	"fmt"
	"net/http"
	"os"
	"time"

	"github.com/spf13/cobra"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"

	"github.com/fecad/internal/crypto"
	lattigobackend "github.com/fecad/internal/crypto/lattigo"
	"github.com/fecad/internal/crypto/null"
)

var rootCmd = &cobra.Command{
	Use:   "fecad",
	Short: "FeCaD — Federated Consent-aware Discovery",
	Long: `FeCaD: homomorphically encrypted decentralised patient discovery.

Entities:
  researcher  Submit encrypted queries; decrypt results.
  proxy       Key-switch coordinator; route queries to providers.
  provider    Evaluate encrypted queries against local encrypted records.
  patient     Issue per-query consent tokens (stub).

Run "fecad <entity> --help" for subcommand details.`,
}

// CLI entry point
func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func init() {
	rootCmd.PersistentFlags().String("backend", "lattigo", "crypto backend: lattigo | he3db | engorgio | patdiscover | null")
	rootCmd.PersistentFlags().String("build-dir", "./bin", "directory with compiled he3db/engorgio binaries")

	// Loopback bench traffic is thousands of requests per task; the stdlib idle
	// caps (2 per host) drop nearly every keep-alive and wedge the socket table
	// in TIME_WAIT. DefaultTransport is shared with benchHTTPClient
	if t, ok := http.DefaultTransport.(*http.Transport); ok {
		t.MaxIdleConnsPerHost = 256
		t.MaxIdleConns = 4096
		t.IdleConnTimeout = 90 * time.Second
	}
}

// global zap logger
func mustLogger() *zap.Logger {
	var (
		log *zap.Logger
		err error
	)
	switch os.Getenv("FECAD_LOG_LEVEL") {
	case "debug", "trace":
		cfg := zap.NewDevelopmentConfig()
		cfg.Level = zap.NewAtomicLevelAt(zapcore.DebugLevel)
		log, err = cfg.Build()
	default:
		log, err = zap.NewProduction()
	}
	if err != nil {
		panic(err)
	}
	zap.ReplaceGlobals(log)
	return log
}

func mustBackendName(cmd *cobra.Command) string {
	name, _ := cmd.Root().PersistentFlags().GetString("backend")
	return name
}

// backend from --backend
func mustBackend(cmd *cobra.Command) crypto.Backend {
	name, _ := cmd.Root().PersistentFlags().GetString("backend")
	buildDir, _ := cmd.Root().PersistentFlags().GetString("build-dir")
	switch name {
	case "null":
		return null.New()
	case "lattigo":
		return lattigobackend.New()
	default:
		// CGO engines link the C++ bridges, so they sit behind the cgo_backends tag
		if b := cgoBackend(name, buildDir); b != nil {
			return b
		}
		panic("unknown backend: " + name)
	}
}
