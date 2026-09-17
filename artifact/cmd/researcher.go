package cmd

import (
	"net/http"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/user"
)

var researcherCmd = &cobra.Command{
	Use:   "researcher",
	Short: "Researcher entity commands",
}

var researcherServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the researcher HTTP server",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		addr, _ := cmd.Flags().GetString("addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		b := mustBackend(cmd)

		srv := user.New(user.Config{
			DataDir: dataDir,
			Backend: b,
			Log:     log,
		})
		log.Info("researcher listening", zap.String("addr", addr))
		return http.ListenAndServe(addr, srv)
	},
}

func init() {
	researcherServeCmd.Flags().String("addr", ":8080", "listen address")
	researcherServeCmd.Flags().String("data-dir", "data/researcher", "directory for keys and results")
	researcherCmd.AddCommand(researcherServeCmd)
	rootCmd.AddCommand(researcherCmd)
}
