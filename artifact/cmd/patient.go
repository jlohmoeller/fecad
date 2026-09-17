package cmd

import (
	"net/http"
	"strconv"
	"strings"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/patient"
)

var patientCmd = &cobra.Command{
	Use:   "patient",
	Short: "Patient entity commands (consent)",
}

var patientServeCmd = &cobra.Command{
	Use:   "serve",
	Short: "Start the patient HTTP server",
	RunE: func(cmd *cobra.Command, args []string) error {
		log := mustLogger()
		addr, _ := cmd.Flags().GetString("addr")
		dataDir, _ := cmd.Flags().GetString("data-dir")
		patientID, _ := cmd.Flags().GetString("patient-id")
		proxyURL, _ := cmd.Flags().GetString("proxy-url")
		providerURL, _ := cmd.Flags().GetString("provider-url")
		recordIDsStr, _ := cmd.Flags().GetString("record-ids")
		runID, _ := cmd.Flags().GetString("run-id")

		var recordIDs []int
		for _, s := range strings.Split(recordIDsStr, ",") {
			s = strings.TrimSpace(s)
			if s == "" {
				continue
			}
			if n, err := strconv.Atoi(s); err == nil {
				recordIDs = append(recordIDs, n)
			}
		}

		srv := patient.New(patient.Config{
			DataDir:     dataDir,
			PatientID:   patientID,
			ProxyURL:    proxyURL,
			ProviderURL: providerURL,
			RecordIDs:   recordIDs,
			RunID:       runID,
			Log:         log,
		})
		srv.Start()
		log.Info("patient listening",
			zap.String("addr", addr),
			zap.String("patient_id", patientID),
			zap.Int("record_ids", len(recordIDs)))
		return http.ListenAndServe(addr, srv)
	},
}

func init() {
	patientServeCmd.Flags().String("addr", ":8084", "listen address")
	patientServeCmd.Flags().String("data-dir", "data/patient", "working directory")
	patientServeCmd.Flags().String("patient-id", "patient0", "unique patient identifier")
	patientServeCmd.Flags().String("proxy-url", "", "proxy base URL, e.g. http://proxy:8082")
	patientServeCmd.Flags().String("provider-url", "", "provider base URL, e.g. http://provider:8081")
	patientServeCmd.Flags().String("record-ids", "", "comma-separated row indices owned by this patient")
	patientServeCmd.Flags().String("run-id", "", "benchmark run identifier for metrics, e.g. task0_nuclear_medicine_q1")
	patientCmd.AddCommand(patientServeCmd)
	rootCmd.AddCommand(patientCmd)
}
