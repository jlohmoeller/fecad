package cmd

import (
	"bufio"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"maps"
	"math/rand"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/spf13/cobra"
	"go.uber.org/zap"

	"github.com/fecad/internal/crypto"
	"github.com/fecad/internal/data_holder"
	"github.com/fecad/internal/evaluator"
	"github.com/fecad/internal/keymanager"
	"github.com/fecad/internal/metrics"
	"github.com/fecad/internal/proxy"
	"github.com/fecad/internal/schema"
	"github.com/fecad/internal/sqlparser"
	"github.com/fecad/internal/user"
)

// proxy's cross-provider ct_R (paper app:protocol step 7)
const aggregateResultID = proxy.AggregateResultID

// benchmark queries per dataset
var datasetQueries = map[string]map[string]string{
	"nuclear_medicine": {
		"q1": "SELECT * FROM patients WHERE whoGrade >= 1",
		"q2": "SELECT * FROM patients WHERE tumorType = 3 AND idhWildType = 1",
		"q3": "SELECT * FROM patients WHERE tumorType = 3 AND idhWildType = 1 AND age <= 60",
		"q4": "SELECT * FROM patients WHERE tumorType = 3 AND idhWildType = 1 AND age <= 60 AND biopsy = 1",
	},
	"mimic_iv": {
		"q1": "SELECT * FROM patients WHERE age >= 0",
		"q2": "SELECT * FROM patients WHERE creatinine_q >= 15",
		"q3": "SELECT * FROM patients WHERE creatinine_q >= 15 AND age >= 60",
		"q4": "SELECT * FROM patients WHERE creatinine_q >= 15 AND age >= 60 AND sex = 1",
	},
	"hcup_nis": {
		"q1": "SELECT * FROM patients WHERE female = 1 AND dx_cervical_ca = 1 AND pr_radical_hyst = 1",
		"q2": "SELECT * FROM patients WHERE dx_oroph = 1 AND pr_pharyn = 1 OR dx_tongue = 1 AND pr_gloss = 1",
		"q3": "SELECT * FROM patients WHERE female = 1 AND dx_breast_ca = 1 AND pr_mastectomy = 1",
	},
}

var benchCmd = &cobra.Command{
	Use:   "bench",
	Short: "Benchmark commands",
}

var benchRunTaskCmd = &cobra.Command{
	Use:   "run-task",
	Short: "Execute one end-to-end benchmark task (used by Slurm array jobs)",
	RunE: func(cmd *cobra.Command, args []string) error {
		taskID, _ := cmd.Flags().GetInt("task-id")
		dataset, _ := cmd.Flags().GetString("dataset")
		query, _ := cmd.Flags().GetString("query")
		records, _ := cmd.Flags().GetInt("records")
		numProviders, _ := cmd.Flags().GetInt("providers")
		outDir, _ := cmd.Flags().GetString("out-dir")
		schemaFile, _ := cmd.Flags().GetString("schema")
		dataFile, _ := cmd.Flags().GetString("data-file")

		consentFraction, _ := cmd.Flags().GetFloat64("consent-fraction")
		splitStorage, _ := cmd.Flags().GetBool("split-storage")
		endpointsDir, _ := cmd.Flags().GetString("provider-endpoints")
		taskDir, _ := cmd.Flags().GetString("task-dir")
		bindHost, _ := cmd.Flags().GetString("bind-host")
		if bindHost != "" {
			benchBindHost = bindHost
		}
		var endpoints []providerEndpoint
		if endpointsDir != "" && splitStorage {
			return fmt.Errorf("--split-storage does not compose with --provider-endpoints: " +
				"the remote services own their directories (use provider serve --evaluator-data-dir)")
		}
		if endpointsDir != "" {
			var epErr error
			endpoints, epErr = loadProviderEndpoints(endpointsDir, numProviders)
			if epErr != nil {
				return epErr
			}
		}
		return runTask(runTaskParams{
			taskID:          taskID,
			dataset:         dataset,
			query:           query,
			records:         records,
			numProviders:    numProviders,
			outDir:          outDir,
			schemaFile:      schemaFile,
			dataFile:        dataFile,
			backend:         mustBackend(cmd),
			backendName:     mustBackendName(cmd),
			consentFraction: consentFraction,
			splitStorage:    splitStorage,
			providerURLs:    endpoints,
			taskDir:         taskDir,
			log:             mustLogger(),
		})
	},
}

var benchRunCmd = &cobra.Command{
	Use:   "run",
	Short: "Run N sequential benchmark tasks",
	RunE: func(cmd *cobra.Command, args []string) error {
		runs, _ := cmd.Flags().GetInt("runs")
		dataset, _ := cmd.Flags().GetString("dataset")
		query, _ := cmd.Flags().GetString("query")
		records, _ := cmd.Flags().GetInt("records")
		numProviders, _ := cmd.Flags().GetInt("providers")
		outDir, _ := cmd.Flags().GetString("out-dir")
		schemaFile, _ := cmd.Flags().GetString("schema")
		splitStorage, _ := cmd.Flags().GetBool("split-storage")
		b := mustBackend(cmd)
		bName := mustBackendName(cmd)
		log := mustLogger()

		for i := range runs {
			log.Info("starting run", zap.Int("run", i), zap.Int("of", runs))
			if err := runTask(runTaskParams{
				taskID:          i,
				dataset:         dataset,
				query:           query,
				records:         records,
				numProviders:    numProviders,
				outDir:          outDir,
				schemaFile:      schemaFile,
				backend:         b,
				backendName:     bName,
				consentFraction: 1.0,
				splitStorage:    splitStorage,
				log:             log,
			}); err != nil {
				log.Error("run failed", zap.Int("run", i), zap.Error(err))
			}
			cleanBinFiles(outDir, log)
		}

		return nil
	},
}

var benchBulkCmd = &cobra.Command{
	Use:   "bulk",
	Short: "Sweep all datasets × queries × record counts",
	RunE: func(cmd *cobra.Command, args []string) error {
		outDir, _ := cmd.Flags().GetString("out-dir")
		b := mustBackend(cmd)
		bName := mustBackendName(cmd)
		log := mustLogger()

		taskID := 0
		for ds, qs := range datasetQueries {
			for q := range qs {
				for _, n := range []int{100, 1000, 5000} {
					log.Info("bulk run",
						zap.String("dataset", ds), zap.String("query", q), zap.Int("records", n))
					if err := runTask(runTaskParams{
						taskID:          taskID,
						dataset:         ds,
						query:           q,
						records:         n,
						numProviders:    1,
						outDir:          outDir,
						backend:         b,
						backendName:     bName,
						consentFraction: 1.0,
						log:             log,
					}); err != nil {
						log.Error("bulk run failed", zap.Error(err))
					}
					cleanBinFiles(outDir, log)
					taskID++
				}
			}
		}
		return nil
	},
}

func init() {
	benchRunTaskCmd.Flags().Int("task-id", 0, "task index")
	benchRunTaskCmd.Flags().String("dataset", "nuclear_medicine", "dataset name")
	benchRunTaskCmd.Flags().String("query", "q1", "query identifier (q1-q4)")
	benchRunTaskCmd.Flags().Int("records", 1000, "number of records per provider")
	benchRunTaskCmd.Flags().Int("providers", 1, "number of provider instances")
	benchRunTaskCmd.Flags().String("out-dir", "results", "output directory")
	benchRunTaskCmd.Flags().String("schema", "", "schema JSON path (default: schemas/<dataset>.json)")
	benchRunTaskCmd.Flags().String("data-file", "", "existing data.json to use (default: generate synthetic)")
	benchRunTaskCmd.Flags().Float64("consent-fraction", 1.0, "fraction of records with consent (0.0–1.0)")
	benchRunTaskCmd.Flags().String("provider-endpoints", "",
		"directory holding provider_<i>.json endpoint files written by already-running "+
			"provider services; enables the multi-host deployment")
	benchRunTaskCmd.Flags().String("task-dir", "",
		"shared working directory for all entity state (default: node-local scratch); "+
			"required with --provider-endpoints so remote providers see the same paths")
	benchRunTaskCmd.Flags().String("bind-host", "",
		"interface the in-process entities listen on (default 127.0.0.1); set to 0.0.0.0 "+
			"so remote providers can reach the key manager and proxy")
	benchRunTaskCmd.Flags().Bool("split-storage", false,
		"run the data holder and the evaluator on disjoint storage; the evaluator "+
			"pulls the store over HTTP (paper app:protocol step C) and never sees sk_h")

	benchRunCmd.Flags().Int("runs", 30, "number of sequential runs")
	benchRunCmd.Flags().String("dataset", "nuclear_medicine", "dataset name")
	benchRunCmd.Flags().String("query", "q1", "query identifier (q1-q4)")
	benchRunCmd.Flags().Int("records", 1000, "records per provider")
	benchRunCmd.Flags().Int("providers", 1, "number of provider instances")
	benchRunCmd.Flags().String("out-dir", "results", "output directory")
	benchRunCmd.Flags().String("schema", "", "schema JSON path")
	benchRunCmd.Flags().Bool("split-storage", false, "disjoint data-holder/evaluator storage")

	benchBulkCmd.Flags().String("out-dir", "results", "output directory")

	benchCmd.AddCommand(benchRunTaskCmd)
	benchCmd.AddCommand(benchRunCmd)
	benchCmd.AddCommand(benchBulkCmd)
	rootCmd.AddCommand(benchCmd)
}

type runTaskParams struct {
	taskID          int
	dataset         string
	query           string
	records         int
	numProviders    int
	outDir          string
	schemaFile      string
	dataFile        string
	backend         crypto.Backend
	backendName     string
	consentFraction float64
	splitStorage    bool
	providerURLs    []providerEndpoint
	taskDir         string
	log             *zap.Logger
}

// one remote data-holder/evaluator pair
// multi-host deployment: this process only orchestrates (paper §6.2)
type providerEndpoint struct {
	DataHolder string `json:"data_holder"`
	Evaluator  string `json:"evaluator"`
}

// compile-time stamp, set via -ldflags "-X .../cmd.BuildTime=<RFC3339>"
// fences off complete.json markers from an older binary so a re-deploy re-runs
var BuildTime string

type completionRecord struct {
	Dataset         string  `json:"dataset"`
	Query           string  `json:"query"`
	Records         int     `json:"records"`
	NumProviders    int     `json:"num_providers"`
	Backend         string  `json:"backend"`
	ConsentFraction float64 `json:"consent_fraction"`
	SchemaFile      string  `json:"schema_file"`
	DataFile        string  `json:"data_file"`
	SplitStorage    bool    `json:"split_storage,omitempty"`
	BuildTime       string  `json:"build_time,omitempty"`
}

func runTask(p runTaskParams) error {
	// persistDir is on the shared FS: per-entity metrics plus the complete.json
	// resume marker, walked by aggregate.go
	persistDir := filepath.Join(p.outDir, fmt.Sprintf("task_%d", p.taskID))
	completePath := filepath.Join(persistDir, "complete.json")
	rec := completionRecord{
		Dataset:         p.dataset,
		Query:           p.query,
		Records:         p.records,
		NumProviders:    p.numProviders,
		Backend:         p.backendName,
		ConsentFraction: p.consentFraction,
		SchemaFile:      p.schemaFile,
		DataFile:        p.dataFile,
		SplitStorage:    p.splitStorage,
		BuildTime:       BuildTime,
	}
	// A matching complete.json skips the task; otherwise wipe the tree so stale
	// keys and metrics cannot leak into the re-run
	if raw, err := os.ReadFile(completePath); err == nil {
		var prev completionRecord
		if json.Unmarshal(raw, &prev) == nil && prev == rec {
			p.log.Info("skipping completed task",
				zap.Int("task", p.taskID),
				zap.String("dataset", p.dataset),
				zap.String("query", p.query))
			return nil
		}
	}
	if _, err := os.Stat(persistDir); err == nil {
		p.log.Info("cleaning stale persist dir",
			zap.Int("task", p.taskID), zap.String("path", persistDir))
		if err := os.RemoveAll(persistDir); err != nil {
			return fmt.Errorf("clean persist dir: %w", err)
		}
	}

	// scratchDir holds every transient artifact (keys, encrypted data, result
	// blobs) on node-local /tmp so concurrent array tasks neither thrash shared
	// storage nor collide. --task-dir overrides it for remote providers, which
	// own part of the tree and outlive this process, so it is left in place
	scratchDir := p.taskDir
	if scratchDir == "" {
		var err error
		scratchDir, err = makeScratchDir(p.taskID, rec)
		if err != nil {
			return fmt.Errorf("scratch dir: %w", err)
		}
		defer func() {
			if rmErr := os.RemoveAll(scratchDir); rmErr != nil {
				p.log.Warn("failed to remove scratch dir",
					zap.String("path", scratchDir), zap.Error(rmErr))
			}
		}()
	} else if err := os.MkdirAll(scratchDir, 0o755); err != nil {
		return fmt.Errorf("task dir: %w", err)
	}

	// Fail-fast disk guard: a CKKS task can spill 100s of GB and wedge the node;
	// scratch and persist often sit on different mounts, so watch both
	stopScratchMon := startDiskMonitor(scratchDir, 95.0, p.log)
	defer stopScratchMon()
	stopOutMon := startDiskMonitor(p.outDir, 95.0, p.log)
	defer stopOutMon()
	// alias: every entity dir is scratchDir/<entity>
	taskDir := scratchDir

	schemaPath := p.schemaFile
	if schemaPath == "" {
		schemaPath = fmt.Sprintf("schemas/%s.json", p.dataset)
	}
	sc, err := schema.Load(schemaPath)
	if err != nil {
		return fmt.Errorf("load schema: %w", err)
	}

	queries, ok := datasetQueries[p.dataset]
	if !ok {
		return fmt.Errorf("unknown dataset %q", p.dataset)
	}
	sqlStr, ok := queries[p.query]
	if !ok {
		return fmt.Errorf("unknown query %q for dataset %q", p.query, p.dataset)
	}
	instructions, err := sqlparser.Parse(sqlStr, sc)
	if err != nil {
		return fmt.Errorf("parse SQL: %w", err)
	}
	p.log.Info("bench task",
		zap.Int("task", p.taskID), zap.String("dataset", p.dataset),
		zap.String("query", p.query), zap.Int("records", p.records),
		zap.String("instructions", instructions))

	researcherDir := filepath.Join(taskDir, "researcher")
	proxyDir := filepath.Join(taskDir, "proxy")
	kmDir := filepath.Join(taskDir, "keymanager")
	for _, d := range []string{taskDir, researcherDir, proxyDir, kmDir} {
		if err := os.MkdirAll(d, 0755); err != nil {
			return err
		}
	}

	rSrv := user.New(user.Config{DataDir: researcherDir, Backend: p.backend, Log: p.log})
	rAddr, err := serveFreePort(rSrv)
	if err != nil {
		return fmt.Errorf("start researcher: %w", err)
	}

	pxSrv := proxy.New(proxy.Config{DataDir: proxyDir, Backend: p.backend, Log: p.log, BenchmarkMode: true})
	pAddr, err := serveFreePort(pxSrv)
	if err != nil {
		return fmt.Errorf("start proxy: %w", err)
	}

	// Key manager (paper §3.3): derives every entity SK and the per-provider
	// KSKs; pushed bytes land as net_in_bytes on the receiver's install-* rows
	kmSrv := keymanager.New(keymanager.Config{DataDir: kmDir, Backend: p.backend, Log: p.log})
	kmAddr, err := serveFreePort(kmSrv)
	if err != nil {
		return fmt.Errorf("start keymanager: %w", err)
	}

	// Per paper app:protocol a provider is split into a data holder (sk_h
	// custodian) and an evaluator (eval-key only); both run here and share the
	// per-provider dir (paper app:consent-envelope merges them into a provider)
	dhAddrs := make([]string, p.numProviders)
	prvAddrs := make([]string, p.numProviders) // evaluator addrs (registered with proxy)
	var allRows []map[string]any
	if p.dataFile != "" {
		raw, err := os.ReadFile(p.dataFile)
		if err != nil {
			return fmt.Errorf("read data file: %w", err)
		}
		if err := json.Unmarshal(raw, &allRows); err != nil {
			return fmt.Errorf("parse data file: %w", err)
		}
	}
	for i := 0; i < p.numProviders; i++ {
		prvDir := filepath.Join(taskDir, fmt.Sprintf("provider_%d", i))
		if err := os.MkdirAll(prvDir, 0755); err != nil {
			return err
		}
		destData := filepath.Join(prvDir, "data.json")
		if allRows != nil {
			start := (i * p.records) % len(allRows)
			slice := make([]map[string]any, p.records)
			// supplied rows carry no consent, so stamp it as the generator does;
			// a row that already states it keeps its own decision
			rng := rand.New(rand.NewSource(int64(p.taskID*100 + i)))
			for j := range slice {
				row := maps.Clone(allRows[(start+j)%len(allRows)])
				if _, ok := row["consented"]; !ok {
					row["consented"] = rng.Float64() < p.consentFraction
				}
				slice[j] = row
			}
			enc, _ := json.MarshalIndent(slice, "", "  ")
			if err := os.WriteFile(destData, enc, 0o644); err != nil {
				return err
			}
		} else {
			if err := generateSyntheticData(sc, p.records, int64(p.taskID*100+i), p.consentFraction, destData); err != nil {
				return err
			}
		}
		// Multi-host: the services already run elsewhere, so only record addresses
		if len(p.providerURLs) > 0 {
			dhAddrs[i] = p.providerURLs[i].DataHolder
			prvAddrs[i] = p.providerURLs[i].Evaluator
			continue
		}

		dhSrv := data_holder.New(data_holder.Config{
			DataDir:    prvDir,
			SchemaPath: schemaPath,
			Backend:    p.backend,
			Log:        p.log,
		})
		dhAddr, err := serveFreePort(dhSrv)
		if err != nil {
			return fmt.Errorf("start data holder %d: %w", i, err)
		}
		dhAddrs[i] = dhAddr

		// --split-storage: the evaluator gets its own dir, materialised from the
		// data holder over HTTP (step C below), so sk_h never lands on it
		evalDir := prvDir
		if p.splitStorage {
			evalDir = evaluatorDir(taskDir, i)
			if err := os.MkdirAll(evalDir, 0755); err != nil {
				return err
			}
		}
		evSrv := evaluator.New(evaluator.Config{
			DataDir:    evalDir,
			SchemaPath: schemaPath,
			Backend:    p.backend,
			Log:        p.log,
		})
		evAddr, err := serveFreePort(evSrv)
		if err != nil {
			return fmt.Errorf("start evaluator %d: %w", i, err)
		}
		prvAddrs[i] = evAddr
	}

	for _, addr := range append([]string{rAddr, pAddr, kmAddr}, prvAddrs...) {
		if err := waitHealthy(addr, 10*time.Second); err != nil {
			return fmt.Errorf("service not ready %s: %w", addr, err)
		}
	}

	runID := fmt.Sprintf("task%d_%s_%s", p.taskID, p.dataset, p.query)

	// Phase 0: the KM derives the researcher SK (paper §3.3) and writes its own
	// km-keygen-* / km-ksk-* / km-setup-total rows
	p.log.Info("phase 0: key manager setup",
		zap.String("entity", "keymanager"),
		zap.String("run_id", runID), zap.Int("providers", p.numProviders))
	if err := postJSON(kmAddr+"/setup", map[string]any{
		"run_id": runID, "num_providers": p.numProviders,
	}); err != nil {
		return fmt.Errorf("keymanager setup: %w", err)
	}

	// Phase 1: researcher pulls its KM-derived SK (bytes land on
	// researcher-install-sk net_in_bytes)
	p.log.Info("phase 1: researcher install SK from KM",
		zap.String("entity", "researcher"), zap.String("run_id", runID))
	if err := postJSON(rAddr+"/install-sk", map[string]string{
		"run_id": runID, "km_url": kmAddr,
	}); err != nil {
		return fmt.Errorf("researcher install-sk: %w", err)
	}

	// Phases 2/2.5/3 interleave per provider so the KM never holds more than one
	// provider's key material: depth-21 BFV eval-mult and KSK files reach
	// hundreds of MB each and broke /tmp for E3 at N≥16
	p.log.Info("phase 2-3: per-provider streamed setup",
		zap.String("entity", "keymanager"),
		zap.Int("providers", p.numProviders), zap.String("run_id", runID))
	patientDir := filepath.Join(taskDir, "patient")
	if err := os.MkdirAll(patientDir, 0755); err != nil {
		return err
	}
	// One real patient runs end-to-end in its own process so VmHWM there is the
	// patient role's RSS alone; the remaining rows are optimised out because one
	// patient already characterises per-patient effort
	patBatch, err := spawnPatientBatch(patientDir, p.log)
	if err != nil {
		return fmt.Errorf("spawn patient subprocess: %w", err)
	}
	defer patBatch.stop()

	// Cross-provider aggregation (paper app:protocol step 7): when the backend can
	// place a result at a slot offset, provider i takes [i*records, (i+1)*records)
	// and the proxy returns one ct_R; otherwise one ciphertext per provider
	aggregateResults := false
	if oa, ok := p.backend.(crypto.OffsetAggregator); ok {
		aggregateResults = p.records*p.numProviders <= oa.MaxAggregateRows()
		if !aggregateResults {
			p.log.Info("federation exceeds one slot row; returning per-provider results",
				zap.Int("rows", p.records*p.numProviders), zap.Int("max", oa.MaxAggregateRows()))
		}
	}
	// Federation-wide row numbering even without aggregation: the proxy keeps one
	// consent map and one cancellation store, so local row ids would collide
	rowOffset := func(i int) int { return i * p.records }

	var samplePatientID string
	var allConsenting []consentEnrollment
	for i := 0; i < p.numProviders; i++ {
		// prvAddrs[i] is the evaluator, registered with the proxy via /install-ksk;
		// dhAddrs[i] is the sk_h-side data holder, driven directly by bench
		dhAddr := dhAddrs[i]
		prvDir := filepath.Join(taskDir, fmt.Sprintf("provider_%d", i))

		if err := postJSON(kmAddr+"/setup-provider", map[string]any{
			"run_id": runID, "provider_id": i, "row_offset": rowOffset(i),
		}); err != nil {
			return fmt.Errorf("keymanager setup-provider %d: %w", i, err)
		}

		// Data-holder side: the sk_h tar lands there (paper app:protocol step A) and
		// records are encrypted under sk_h before leaving it (step C)
		if err := postJSON(dhAddr+"/install-keys", map[string]any{
			"run_id":      runID,
			"km_url":      kmAddr,
			"provider_id": i,
		}); err != nil {
			return fmt.Errorf("provider %d install-keys: %w", i, err)
		}
		if err := postJSON(dhAddr+"/encrypt-data", map[string]string{"run_id": runID}); err != nil {
			return fmt.Errorf("provider %d encrypt-data: %w", i, err)
		}

		dataFile := filepath.Join(prvDir, "data.json")
		patients, err := buildConsentingMap(dataFile, i)
		if err != nil {
			return fmt.Errorf("read consenting patients provider %d: %w", i, err)
		}
		// Blinding and cancellation CTs stay on disk: provider-evaluate cost scales
		// with the blindings/ dir size, which E1-E5 need realistic
		if err := p.backend.SetupAllPatientsBlinding(prvDir, proxyDir, patients); err != nil {
			return fmt.Errorf("blinding setup provider %d: %w", i, err)
		}
		p.log.Info("patients blinded", zap.String("entity", "patient"),
			zap.Int("provider", i), zap.Int("enrolled", len(patients)))

		// Every consenting patient registers and signs the envelope (paper §A.1),
		// so consent cost scales with the fraction; rows here are federation-wide
		off := rowOffset(i)
		for id, rows := range patients {
			global := make([]int, len(rows))
			for j, r := range rows {
				global[j] = off + r
			}
			allConsenting = append(allConsenting, consentEnrollment{PatientID: id, RecordIDs: global})
		}

		// Step C (paper app:protocol): ship the CT store, eval key, pk_h and +r_p
		// blinding CTs to the evaluator; only the disjoint deployment pays this
		if p.splitStorage {
			if err := postJSON(prvAddrs[i]+"/install-store", map[string]any{
				"run_id":          runID,
				"data_holder_url": dhAddr,
			}); err != nil {
				return fmt.Errorf("provider %d install-store: %w", i, err)
			}
		}
		if samplePatientID == "" {
			for id := range patients {
				samplePatientID = id
				break
			}
		}

		regBody := map[string]any{
			"run_id":           runID,
			"km_url":           kmAddr,
			"provider_id":      fmt.Sprintf("provider_%d", i),
			"provider_index":   i,
			"provider_address": prvAddrs[i],
			"provider_sk_dir":  prvDir,
			"row_offset":       rowOffset(i),
		}
		if err := postJSON(pAddr+"/install-ksk", regBody); err != nil {
			return fmt.Errorf("proxy install-ksk provider %d: %w", i, err)
		}

		// Drop the KM-side copies to keep peak KM disk bounded
		if err := postJSON(kmAddr+"/cleanup-provider", map[string]any{
			"provider_id": i,
		}); err != nil {
			return fmt.Errorf("keymanager cleanup-provider %d: %w", i, err)
		}
	}

	// The subprocess BLS-keygens and posts one /consent/register-batch, timing
	// patient-consent in its own process. Cancellation CTs live in the backend's
	// shared store, so the registration carries an empty hex
	if err := enrollConsentingPatients(patBatch.baseURL, pAddr, runID, allConsenting); err != nil {
		return fmt.Errorf("patient enrollment: %w", err)
	}
	p.log.Info("patients enrolled", zap.String("entity", "patient"),
		zap.Int("consenting", len(allConsenting)), zap.String("sample", samplePatientID))

	// Phase 4: query dispatch. queryStart spans dispatch through decryption
	// (researcher-query-total)
	queryStart := time.Now()
	p.log.Info("phase 4: query dispatched", zap.String("entity", "proxy"), zap.String("run_id", runID), zap.String("instructions", instructions))

	// Phase 4a: EQ literals are encrypted under sk_R (paper §3.4); range
	// conditions keep their plaintext value field
	eqLiterals := extractEQLiterals(instructions)
	var literals []byte
	if len(eqLiterals) > 0 {
		encResp, encErr := postJSONResp(rAddr+"/encrypt-query", map[string]any{
			"run_id":       runID,
			"instructions": instructions,
			"values":       eqLiterals,
		})
		if encErr != nil {
			return fmt.Errorf("researcher encrypt-query: %w", encErr)
		}
		literals = encResp
	}

	// In parallel with /query-request every patient pushes its BLS envelope
	// (paper §A.1); the proxy waits in collectConsent and folds them into σ_multi
	qd := computeQD(instructions, runID)
	var envelopeWG sync.WaitGroup
	envelopeWG.Add(1)
	go func() {
		defer envelopeWG.Done()
		signAllPatients(patBatch.baseURL, pAddr, qd, runID, p.log)
	}()

	// Small fields go in the query string, the body is the raw literal CT
	queryParams := url.Values{
		"run_id":       {runID},
		"instructions": {instructions},
		"rows":         {strconv.Itoa(p.records)},
		"aggregate":    {strconv.FormatBool(aggregateResults)},
	}
	resp, err := postBinaryResp(pAddr+"/query-request?"+queryParams.Encode(), literals)
	envelopeWG.Wait()
	if err != nil {
		return fmt.Errorf("query-request: %w", err)
	}

	// Phase 5: researcher decrypts results
	p.log.Info("phase 5: result decryption", zap.String("entity", "researcher"), zap.String("run_id", runID))
	var proxyResults []struct {
		ProviderID string `json:"provider_id"`
		Data       []byte `json:"data"`
		Error      string `json:"error"`
	}
	if err := json.Unmarshal(resp, &proxyResults); err != nil {
		return fmt.Errorf("decode proxy results: %w", err)
	}
	phase5Start := time.Now()
	decryptedByProvider := make(map[string][]byte, len(proxyResults))
	for _, res := range proxyResults {
		if res.Error != "" {
			p.log.Warn("provider error", zap.String("provider", res.ProviderID), zap.String("err", res.Error))
			continue
		}
		decRows := p.records
		if res.ProviderID == aggregateResultID {
			decRows = p.records * p.numProviders
		}
		req, err := http.NewRequest(http.MethodPost,
			fmt.Sprintf("%s/decrypt?rows=%d&run_id=%s", rAddr, decRows, runID),
			bytes.NewReader(res.Data))
		if err != nil {
			p.log.Warn("decrypt request", zap.String("provider", res.ProviderID), zap.Error(err))
			continue
		}
		req.Header.Set("Content-Type", "application/octet-stream")
		decResp, err := benchHTTPClient.Do(req)
		if err != nil {
			p.log.Warn("decrypt failed", zap.String("provider", res.ProviderID), zap.Error(err))
			continue
		}
		body, readErr := io.ReadAll(decResp.Body)
		decResp.Body.Close()
		if readErr != nil {
			p.log.Warn("decrypt read body", zap.String("provider", res.ProviderID), zap.Error(readErr))
			continue
		}
		decryptedByProvider[res.ProviderID] = body
	}

	rWriter := metrics.NewWriter(researcherDir)

	// Duration only: per-call CT bytes already ride the researcher-decrypt rows
	dMeta := metrics.ParseRunID(runID)
	dMeta.Status = fmt.Sprintf("providers=%d", p.numProviders)
	rWriter.Record("researcher-decrypt-total", time.Since(phase5Start), dMeta, 0, 0)

	// End-to-end latency, duration only: bytes are on the per-call phases
	qMeta := metrics.ParseRunID(runID)
	qMeta.Status = fmt.Sprintf("providers=%d", p.numProviders)
	rWriter.Record("researcher-query-total", time.Since(queryStart), qMeta, 0, 0)

	// HE bits against the plaintext predicate over consenting rows, no services
	scoreAccuracy(p, taskDir, instructions, sc, decryptedByProvider, runID, rWriter)

	logAndCleanKeys(taskDir, researcherDir, runID, p.numProviders, p.log)

	// Mirror per-entity metrics to the shared FS; everything else in scratch is
	// dropped by the defer above
	if err := mirrorMetrics(taskDir, persistDir, p.numProviders, p.splitStorage, p.log); err != nil {
		return fmt.Errorf("mirror metrics: %w", err)
	}

	p.log.Info("task complete", zap.String("run_id", runID))
	data, _ := json.MarshalIndent(rec, "", "  ")
	if err := os.MkdirAll(persistDir, 0o755); err == nil {
		_ = os.WriteFile(completePath, data, 0o644)
	}
	return nil
}

// disk usage watchdog
// log.Fatal on trip: the parent dies without running its cleanup defers so the
// operator can inspect what filled the disk. Returns a stop callback
func startDiskMonitor(dir string, thresholdPct float64, log *zap.Logger) func() {
	done := make(chan struct{})
	go func() {
		ticker := time.NewTicker(2 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				var st syscall.Statfs_t
				if err := syscall.Statfs(dir, &st); err != nil {
					// The dir can briefly vanish in a post-cleanup race, so keep polling
					log.Debug("statfs failed", zap.String("dir", dir), zap.Error(err))
					continue
				}
				total := float64(st.Blocks) * float64(st.Bsize)
				if total <= 0 {
					continue
				}
				avail := float64(st.Bavail) * float64(st.Bsize)
				usedPct := (total - avail) / total * 100
				if usedPct >= thresholdPct {
					log.Fatal("disk usage threshold exceeded — aborting task",
						zap.String("dir", dir),
						zap.Float64("used_pct", usedPct),
						zap.Float64("threshold_pct", thresholdPct))
				}
			}
		}
	}()
	return func() { close(done) }
}

// unique scratch dir
// the param hash keeps a retry with different params off a previous tree
func makeScratchDir(taskID int, rec completionRecord) (string, error) {
	h := sha256.New()
	_ = json.NewEncoder(h).Encode(rec)
	tag := hex.EncodeToString(h.Sum(nil))[:12]
	dir := filepath.Join(os.TempDir(),
		fmt.Sprintf("fecad_task_%d_pid%d_%s", taskID, os.Getpid(), tag))
	if err := os.RemoveAll(dir); err != nil {
		return "", err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	return dir, nil
}

// copy metrics into persistDir
func mirrorMetrics(scratchDir, persistDir string, numProviders int, splitStorage bool, log *zap.Logger) error {
	entities := []string{"researcher", "proxy", "keymanager", "patient"}
	for i := 0; i < numProviders; i++ {
		entities = append(entities, fmt.Sprintf("provider_%d", i))
		if splitStorage {
			// The evaluator writes its own metrics file into its separate dir
			entities = append(entities, filepath.Base(evaluatorDir(scratchDir, i)))
		}
	}
	for _, ent := range entities {
		src := filepath.Join(scratchDir, ent, "performance_metrics.csv")
		if _, err := os.Stat(src); err != nil {
			// An entity may not have written metrics this run
			continue
		}
		dstDir := filepath.Join(persistDir, ent)
		if err := os.MkdirAll(dstDir, 0o755); err != nil {
			return err
		}
		dst := filepath.Join(dstDir, "performance_metrics.csv")
		if err := copyFile(src, dst); err != nil {
			log.Warn("mirror metrics failed",
				zap.String("src", src), zap.String("dst", dst), zap.Error(err))
		}
	}
	return nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.OpenFile(dst, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		return err
	}
	return out.Close()
}

// record key and CT-store sizes
// key-size rows carry one key file, ct-store rows sum a directory
// (blindings/, cancellations/), both on the owning entity's writer
func logAndCleanKeys(taskDir, researcherDir, runID string, numProviders int, log *zap.Logger) {
	base := metrics.ParseRunID(runID)
	proxyDir := filepath.Join(taskDir, "proxy")

	type keySpec struct {
		path   string
		status string
		w      *metrics.Writer
	}

	rW := metrics.NewWriter(researcherDir)
	pxW := metrics.NewWriter(proxyDir)

	var specs []keySpec
	specs = append(specs, keySpec{filepath.Join(researcherDir, "secret_key.bin"), "researcher-sk", rW})
	for i := range numProviders {
		prvDir := filepath.Join(taskDir, fmt.Sprintf("provider_%d", i))
		prvW := metrics.NewWriter(prvDir)
		specs = append(specs,
			keySpec{filepath.Join(prvDir, "secret_key.bin"), "sk", prvW},
			// usually already removed by the provider; catches early-exit paths
			keySpec{filepath.Join(prvDir, "eval_key.bin"), "evalkey", prvW},
		)
	}
	if matches, _ := filepath.Glob(filepath.Join(proxyDir, "ksk_*.bin")); len(matches) > 0 {
		for _, p := range matches {
			specs = append(specs, keySpec{p, filepath.Base(p), pxW})
		}
	}

	for _, spec := range specs {
		info, err := os.Stat(spec.path)
		if err != nil {
			continue
		}
		meta := base
		meta.Status = spec.status
		spec.w.RecordSized("key-size", 0, meta, 0, 0, info.Size())
		if err := os.Remove(spec.path); err != nil {
			log.Warn("failed to remove key file", zap.String("path", spec.path), zap.Error(err))
		}
	}

	// CT stores are on-disk only, never wire-transferred: artifact_size_bytes
	stores := []struct {
		dir    string
		status string
		w      *metrics.Writer
	}{
		{filepath.Join(proxyDir, "cancellations"), "cancellations", pxW},
	}
	for i := range numProviders {
		prvDir := filepath.Join(taskDir, fmt.Sprintf("provider_%d", i))
		stores = append(stores, struct {
			dir    string
			status string
			w      *metrics.Writer
		}{filepath.Join(prvDir, "blindings"), "blindings", metrics.NewWriter(prvDir)})
	}
	for _, st := range stores {
		size, ok := dirBytes(st.dir)
		if !ok {
			continue
		}
		meta := base
		meta.Status = st.status
		st.w.RecordSized("ct-store", 0, meta, 0, 0, size)
	}
}

// split-storage evaluator dir
// the "provider" prefix keeps aggregate.go's entity classification intact
func evaluatorDir(taskDir string, i int) string {
	return filepath.Join(taskDir, fmt.Sprintf("provider_%d_evaluator", i))
}

// total bytes under dir
func dirBytes(dir string) (int64, bool) {
	info, err := os.Stat(dir)
	if err != nil || !info.IsDir() {
		return 0, false
	}
	var total int64
	_ = filepath.WalkDir(dir, func(_ string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		fi, ferr := d.Info()
		if ferr == nil {
			total += fi.Size()
		}
		return nil
	})
	return total, true
}

// remove .bin files under root
func cleanBinFiles(root string, log *zap.Logger) {
	removed := 0
	err := filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() || filepath.Ext(d.Name()) != ".bin" {
			return nil
		}
		if rmErr := os.Remove(path); rmErr != nil {
			log.Warn("failed to remove bin file", zap.String("path", path), zap.Error(rmErr))
			return nil
		}
		removed++
		return nil
	})
	if err != nil {
		log.Warn("clean bin files failed", zap.String("root", root), zap.Error(err))
	}
	log.Info("cleaned bin files", zap.String("root", root), zap.Int("removed", removed))
}

func postJSON(url string, body any) error {
	_, err := postJSONResp(url, body)
	return err
}

// Finite total timeout: DefaultClient's zero Timeout lets a wedged in-process
// server burn the whole slurm walltime. 48h covers the longest legitimate call
var benchHTTPClient = &http.Client{Timeout: 48 * time.Hour}

// POST raw ciphertext bytes
func postBinaryResp(url string, body []byte) ([]byte, error) {
	req, err := http.NewRequest(http.MethodPost, url, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/octet-stream")
	resp, err := benchHTTPClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	var buf bytes.Buffer
	buf.ReadFrom(resp.Body)
	if resp.StatusCode >= 300 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, buf.String())
	}
	return buf.Bytes(), nil
}

func postJSONResp(url string, body any) ([]byte, error) {
	b, _ := json.Marshal(body)
	req, err := http.NewRequest(http.MethodPost, url, bytes.NewReader(b))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := benchHTTPClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	var buf bytes.Buffer
	buf.ReadFrom(resp.Body)
	if resp.StatusCode >= 300 {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, buf.String())
	}
	return buf.Bytes(), nil
}

func waitHealthy(addr string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		resp, err := http.Get(addr + "/health")
		if err == nil && resp.StatusCode == 200 {
			resp.Body.Close()
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("timeout after %s", timeout)
}

// interface the in-process entities listen on
// loopback isolates concurrent tasks; multi-host sets 0.0.0.0 so remote
// providers can reach the key manager and proxy
var benchBindHost = "127.0.0.1"

// serve h on an OS-assigned port
func serveFreePort(h http.Handler) (string, error) {
	ln, err := net.Listen("tcp", net.JoinHostPort(benchBindHost, "0"))
	if err != nil {
		return "", err
	}
	go func() { _ = http.Serve(ln, h) }()
	host := benchBindHost
	if host == "0.0.0.0" {
		// Remote entities cannot dial 0.0.0.0
		if hn, hErr := os.Hostname(); hErr == nil {
			host = hn
		}
	}
	_, port, _ := net.SplitHostPort(ln.Addr().String())
	return "http://" + net.JoinHostPort(host, port), nil
}

// wait for provider endpoint files
func loadProviderEndpoints(dir string, n int) ([]providerEndpoint, error) {
	deadline := time.Now().Add(10 * time.Minute)
	out := make([]providerEndpoint, n)
	for {
		missing := 0
		for i := 0; i < n; i++ {
			raw, err := os.ReadFile(filepath.Join(dir, fmt.Sprintf("provider_%d.json", i)))
			if err != nil {
				missing++
				continue
			}
			if err := json.Unmarshal(raw, &out[i]); err != nil {
				return nil, fmt.Errorf("parse endpoint %d: %w", i, err)
			}
		}
		if missing == 0 {
			return out, nil
		}
		if time.Now().After(deadline) {
			return nil, fmt.Errorf("only %d of %d provider endpoints appeared in %s", n-missing, n, dir)
		}
		time.Sleep(2 * time.Second)
	}
}

// spawned child plus its base URL
type subproc struct {
	cmd     *exec.Cmd
	baseURL string
}

// SIGTERM, then SIGKILL; nil-safe
func (s *subproc) stop() {
	if s == nil || s.cmd == nil || s.cmd.Process == nil {
		return
	}
	_ = s.cmd.Process.Signal(syscall.SIGTERM)
	done := make(chan struct{})
	go func() { _, _ = s.cmd.Process.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		_ = s.cmd.Process.Kill()
		<-done
	}
}

// spawn the patient subprocess
// the child announces its bound address on stdout as "LISTENING addr=…"; its
// own process makes VmHWM there the patient's peak RSS alone
func spawnPatientBatch(dataDir string, log *zap.Logger) (*subproc, error) {
	exe, err := os.Executable()
	if err != nil {
		return nil, fmt.Errorf("locate self: %w", err)
	}
	cmd := exec.Command(exe, "patient", "batch", "serve",
		"--addr=127.0.0.1:0",
		"--data-dir="+dataDir)
	cmd.Stderr = os.Stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start: %w", err)
	}

	addrCh := make(chan string, 1)
	errCh := make(chan error, 1)
	go func() {
		rdr := bufio.NewReader(stdout)
		for {
			line, err := rdr.ReadString('\n')
			if err != nil {
				errCh <- err
				return
			}
			if rest, ok := strings.CutPrefix(strings.TrimSpace(line), "LISTENING addr="); ok {
				addrCh <- rest
				go io.Copy(io.Discard, rdr) // drain remaining stdout so the child never blocks on a full pipe
				return
			}
		}
	}()

	select {
	case addr := <-addrCh:
		base := "http://" + addr
		if err := waitHealthy(base, 5*time.Second); err != nil {
			_ = cmd.Process.Kill()
			return nil, fmt.Errorf("patient subprocess unhealthy: %w", err)
		}
		log.Info("patient subprocess ready", zap.String("addr", addr))
		return &subproc{cmd: cmd, baseURL: base}, nil
	case e := <-errCh:
		_ = cmd.Process.Kill()
		return nil, fmt.Errorf("patient subprocess exited before LISTENING: %w", e)
	case <-time.After(10 * time.Second):
		_ = cmd.Process.Kill()
		return nil, fmt.Errorf("patient subprocess timed out before LISTENING")
	}
}

// consenting patient-id → row ids
// ids are "prov{i}_row{j}" to avoid cross-provider collisions; only the consent
// flag is decoded so peak heap stays bounded at 100k records
func buildConsentingMap(dataFile string, providerIdx int) (map[string][]int, error) {
	raw, err := os.ReadFile(dataFile)
	if err != nil {
		return nil, err
	}
	var rows []struct {
		Consented bool `json:"consented"`
	}
	if err := json.Unmarshal(raw, &rows); err != nil {
		return nil, err
	}
	patients := make(map[string][]int)
	for rowIdx, row := range rows {
		if !row.Consented {
			continue
		}
		patients[fmt.Sprintf("prov%d_row%d", providerIdx, rowIdx)] = []int{rowIdx}
	}
	return patients, nil
}

// query descriptor
// must stay byte-identical to proxy.makeQD
func computeQD(instructions, runID string) string {
	data, _ := json.Marshal(map[string]string{"instructions": instructions, "run_id": runID})
	h := sha256.Sum256(data)
	return hex.EncodeToString(h[:])
}

// scaled literals, one per condition
// every backend's EvaluatePredicateEncLiterals indexes this array positionally
// and rejects a mismatched count, so range conditions get a slot too even
// though paper §3.4 would leave them plaintext
func extractEQLiterals(instructions string) []int64 {
	if instructions == "" {
		return nil
	}
	var out []int64
	for _, cond := range strings.Split(instructions, ";") {
		fields := strings.Split(cond, ",")
		if len(fields) < 3 {
			continue
		}
		v, err := strconv.ParseInt(fields[1], 10, 64)
		if err != nil {
			continue
		}
		out = append(out, v)
	}
	return out
}

// one patient's registration payload
type consentEnrollment struct {
	PatientID         string `json:"patient_id"`
	RecordIDs         []int  `json:"record_ids"`
	CancellationCTHex string `json:"cancellation_ct_hex,omitempty"`
}

// register patients in chunks
func enrollConsentingPatients(patBatchURL, proxyURL, runID string, patients []consentEnrollment) error {
	const chunk = 20000
	for start := 0; start < len(patients); start += chunk {
		end := min(start+chunk, len(patients))
		if err := postJSON(patBatchURL+"/enroll-batch", map[string]any{
			"patients":  patients[start:end],
			"proxy_url": proxyURL,
			"run_id":    runID,
		}); err != nil {
			return err
		}
	}
	return nil
}

// per-query envelope round
// the consent window is closed unconditionally so collectConsent does not sit
// on its deadline; the signing itself runs in the patient subprocess
func signAllPatients(patBatchURL, proxyURL, qd, runID string, log *zap.Logger) {
	defer func() {
		closeBody, _ := json.Marshal(map[string]string{"qd": qd})
		if resp, err := benchHTTPClient.Post(proxyURL+"/consent/close",
			"application/json", bytes.NewReader(closeBody)); err == nil {
			resp.Body.Close()
		} else {
			log.Warn("consent close failed", zap.Error(err))
		}
	}()
	if err := postJSON(patBatchURL+"/sign-all", map[string]any{
		"proxy_url": proxyURL,
		"qd":        qd,
		"run_id":    runID,
	}); err != nil {
		log.Warn("patient envelope signing failed", zap.Error(err))
	}
}

func generateSyntheticData(sc *schema.Schema, count int, seed int64, consentFraction float64, dest string) error {
	rng := rand.New(rand.NewSource(seed))
	rows := make([]map[string]any, count)
	for i := range rows {
		row := make(map[string]any, len(sc.Columns)+1)
		for _, col := range sc.Columns {
			maxVal := min((1<<col.Bits)-1, 65535)
			row[col.Name] = rng.Intn(maxVal + 1)
		}
		row["consented"] = rng.Float64() < consentFraction
		rows[i] = row
	}
	data, err := json.MarshalIndent(rows, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(dest, data, 0o644)
}
