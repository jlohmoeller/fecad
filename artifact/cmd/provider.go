package cmd

import (
	"net/http"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/data_holder"
	"github.com/fecad/internal/evaluator"
)

// Legacy composite command: the split lives in internal/data_holder (sk_h
// custodian) and internal/evaluator (eval-key only) per paper app:protocol,
// but both servers here share one data dir (paper §5 merges them)

var providerCmd = &cobra.Command{
	Use:   "provider",
	Short: "Provider entity commands (composite: data holder + evaluator)",
}

var providerServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the data-holder and evaluator HTTP servers",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		dhAddr, _ := cmd.Flags().GetString("data-holder-addr")
		evAddr, _ := cmd.Flags().GetString("evaluator-addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		evalDir, _ := cmd.Flags().GetString("evaluator-data-dir")
		if evalDir == "" {
			evalDir = dataDir
		}
		schema, _ := cmd.Flags().GetString("schema")
		b := mustBackend(cmd)

		dhSrv := data_holder.New(data_holder.Config{
			DataDir:    dataDir,
			SchemaPath: schema,
			Backend:    b,
			Log:        log,
		})
		evSrv := evaluator.New(evaluator.Config{
			DataDir:    evalDir,
			SchemaPath: schema,
			Backend:    b,
			Log:        log,
		})

		errCh := make(chan error, 2)
		go func() {
			log.Info("data holder listening", zap.String("addr", dhAddr))
			errCh <- http.ListenAndServe(dhAddr, dhSrv)
		}()
		go func() {
			log.Info("evaluator listening", zap.String("addr", evAddr))
			errCh <- http.ListenAndServe(evAddr, evSrv)
		}()
		return <-errCh
	},
}

func init() {
	providerServeCmd.Flags().String("data-holder-addr", ":8081", "data-holder listen address")
	providerServeCmd.Flags().String("evaluator-addr", ":8082", "evaluator listen address")
	providerServeCmd.Flags().String("data-dir", "data/provider", "working directory with data.json")
	providerServeCmd.Flags().String("evaluator-data-dir", "",
		"separate working directory for the evaluator (default: same as --data-dir); "+
			"set it to keep sk_h off the evaluator's storage and populate it via POST /install-store")
	providerServeCmd.Flags().String("schema", "", "path to schema JSON file")
	providerCmd.AddCommand(providerServeCmd)
	rootCmd.AddCommand(providerCmd)
}
