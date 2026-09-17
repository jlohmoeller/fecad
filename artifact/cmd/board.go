package cmd

import (
	"encoding/json"
	"net/http"
	"os"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/board"
)

var boardCmd = &cobra.Command{
	Use:   "board",
	Short: "Review board entity commands (query admission)",
}

var boardServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the review board HTTP server",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		addr, _ := cmd.Flags().GetString("addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		polFile, _ := cmd.Flags().GetString("policies")

		var policies []board.TierAPolicy
		if polFile != "" {
			data, err := os.ReadFile(polFile)
			if err != nil {
				log.Warn("could not read policies file", zap.String("path", polFile), zap.Error(err))
			} else {
				var wrapper struct {
					TierA []board.TierAPolicy `json:"tier_a"`
				}
				if err := json.Unmarshal(data, &wrapper); err != nil {
					log.Warn("could not parse policies file", zap.Error(err))
				} else {
					policies = wrapper.TierA
				}
			}
		}

		srv := board.New(board.Config{
			DataDir:       dataDir,
			TierAPolicies: policies,
			Log:           log,
		})
		log.Info("board listening", zap.String("addr", addr),
			zap.Int("tier_a_policies", len(policies)))
		return http.ListenAndServe(addr, srv)
	},
}

func init() {
	boardServeCmd.Flags().String("addr", ":8083", "listen address")
	boardServeCmd.Flags().String("data-dir", "data/board", "working directory for keys")
	boardServeCmd.Flags().String("policies", "", "path to policies.json (Tier-A auto-approve rules)")
	boardCmd.AddCommand(boardServeCmd)
	rootCmd.AddCommand(boardCmd)
}
