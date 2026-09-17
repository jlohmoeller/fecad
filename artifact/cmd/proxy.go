package cmd

import (
	"net/http"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/proxy"
)

var proxyCmd = &cobra.Command{
	Use:   "proxy",
	Short: "Proxy entity commands",
}

var proxyServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the proxy HTTP server",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		addr, _ := cmd.Flags().GetString("addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		b := mustBackend(cmd)

		srv := proxy.New(proxy.Config{
			DataDir:    dataDir,
			ListenAddr: addr,
			Backend:    b,
			Log:        log,
		})
		log.Info("proxy listening", zap.String("addr", addr))
		return http.ListenAndServe(addr, srv)
	},
}

func init() {
	proxyServeCmd.Flags().String("addr", ":8082", "listen address")
	proxyServeCmd.Flags().String("data-dir", "data/proxy", "working directory")
	proxyCmd.AddCommand(proxyServeCmd)
	rootCmd.AddCommand(proxyCmd)
}
