package cmd

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"strings"

	"github.com/spf13/cobra"

	"github.com/fecad/internal/schema"
)

var dataCmd = &cobra.Command{
	Use:   "data",
	Short: "Dataset utilities",
}

var dataGenerateCmd = &cobra.Command{
	Use:   "generate",
	Short: "Generate a synthetic dataset from a schema",
	RunE: func(cmd *cobra.Command, args []string) error {
		schemaPath, _ := cmd.Flags().GetString("schema")
		count, _ := cmd.Flags().GetInt("count")
		seed, _ := cmd.Flags().GetInt64("seed")
		outPath, _ := cmd.Flags().GetString("out")

		sc, err := schema.Load(schemaPath)
		if err != nil {
			return fmt.Errorf("load schema: %w", err)
		}

		rng := rand.New(rand.NewSource(seed))
		rows := make([]map[string]any, count)
		for i := range rows {
			row := make(map[string]any, len(sc.Columns))
			for _, col := range sc.Columns {
				maxVal := (1 << col.Bits) - 1
				if maxVal > 65535 {
					maxVal = 65535
				}
				row[col.Name] = rng.Intn(maxVal + 1)
			}
			rows[i] = row
		}

		out, _ := json.MarshalIndent(rows, "", "  ")
		if outPath == "" || outPath == "-" {
			fmt.Println(strings.TrimRight(string(out), "\n"))
			return nil
		}
		return os.WriteFile(outPath, out, 0o644)
	},
}

func init() {
	dataGenerateCmd.Flags().String("schema", "", "path to schema JSON (required)")
	dataGenerateCmd.Flags().Int("count", 1000, "number of rows to generate")
	dataGenerateCmd.Flags().Int64("seed", 42, "random seed")
	dataGenerateCmd.Flags().String("out", "", "output path (default: stdout)")
	_ = dataGenerateCmd.MarkFlagRequired("schema")

	dataCmd.AddCommand(dataGenerateCmd)
	rootCmd.AddCommand(dataCmd)
}
