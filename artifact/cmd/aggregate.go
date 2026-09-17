package cmd

// bench aggregate collapses the per-entity performance_metrics.csv files under
// --results-dir into four CSVs: eN.csv (raw rows plus the dimensions parsed out
// of the path), aggregate.csv (per group: n, mean/std/min/max and the 25/50/75th
// percentiles of duration, mean/max of bytes and RAM), params.csv (one row per
// complete.json marker) and incomplete.csv (every task that did not finish)
//
// complete.json is written only after a full successful run, so its absence
// marks an unfinished job. incomplete.csv statuses: partial (metric rows but no
// marker), no-output (empty task dir), in-progress (in squeue, no task dir yet)
// and ghost (a SLURM log but no task dir at all — the run died before the
// end-of-task mirror, typically ENOSPC). squeue state overrides any log-derived
// failure, and array indices are reused across backends, so a log's backend
// (from its .out first line) is matched before its reason is trusted
//
// Results layout (relative to --results-dir):
//
//	e1_backends/<backend>/<query>/task_<N>/<entity>/performance_metrics.csv
//	e2_records/<backend>/<records>/<query>/task_<N>/<entity>/...
//	e3_providers/<backend>/<n_providers>/task_<N>/<entity>/...
//	e4_datasets/<backend>/<dataset>/<query>/task_<N>/<entity>/...
//	e5_consent/<backend>/<frac_tag>/task_<N>/<entity>/...
//	e6_split/<backend>/<storage>/task_<N>/<entity>/...
//	e7_wan/<backend>/<wan_profile>/<n_providers>/task_<N>/<entity>/...
//	e8_cpuwide/<backend>/<n_providers>/task_<N>/<entity>/...
//	e9_multihost/<backend>/<n_providers>/task_<N>/<entity>/...

import (
	"cmp"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"

	"github.com/spf13/cobra"
)

var knownBackends = map[string]bool{
	"lattigo": true, "he3db": true, "engorgio": true, "patdiscover": true, "null": true,
}

// one experiment's layout
// dims are the path components between <backend> and task_<N>, in order
type expSpec struct {
	name   string
	subdir string
	dims   []string
}

var experiments = []expSpec{
	{"e1", "e1_backends", []string{"query"}},
	{"e2", "e2_records", []string{"records", "query"}},
	{"e3", "e3_providers", []string{"n_providers"}},
	{"e4", "e4_datasets", []string{"dataset", "query"}},
	{"e5", "e5_consent", []string{"consent_fraction"}},
	{"e6", "e6_split", []string{"storage"}},
	{"e7", "e7_wan", []string{"wan_profile", "n_providers"}},
	{"e8", "e8_cpuwide", []string{"n_providers"}},
	{"e9", "e9_multihost", []string{"n_providers"}},
}

type metricRow struct {
	experiment  string
	backend     string
	query       string
	records     string
	nProviders  string
	dataset     string
	consentFrac string
	storage     string
	wanProfile  string
	taskID      int
	entity      string
	entityType  string
	timestamp   string
	phase       string
	durationSec float64
	status      string
	netOut      int64
	netIn       int64
	artifactSz  int64
	peakRAMMB   float64
}

func (r metricRow) dim(name string) string {
	switch name {
	case "query":
		return r.query
	case "records":
		return r.records
	case "n_providers":
		return r.nProviders
	case "dataset":
		return r.dataset
	case "consent_fraction":
		return r.consentFrac
	case "storage":
		return r.storage
	case "wan_profile":
		return r.wanProfile
	}
	return ""
}

type fileJob struct {
	path    string
	spec    expSpec
	backend string
	dimVals []string
	taskID  int
	entity  string
}

// task_<N> without complete.json
type incompleteTask struct {
	experiment string
	backend    string
	dimVals    []string
	taskID     int
}

// one task's recovered config
type taskParams struct {
	experiment string
	backend    string
	taskID     int
	rec        completionRecord
}

var benchAggregateCmd = &cobra.Command{
	Use:   "aggregate",
	Short: "Collect eval performance_metrics.csv files into per-experiment and aggregate CSVs, and list incomplete jobs",
	RunE: func(cmd *cobra.Command, args []string) error {
		resultsDir, _ := cmd.Flags().GetString("results-dir")
		outDir, _ := cmd.Flags().GetString("out-dir")
		logsDir, _ := cmd.Flags().GetString("logs-dir")
		workers, _ := cmd.Flags().GetInt("workers")
		if workers < 1 {
			workers = 1
		}
		return runAggregate(resultsDir, outDir, logsDir, workers)
	},
}

func init() {
	benchAggregateCmd.Flags().String("results-dir", "${FECAD_SHARED:-/srv/fecad}/results", "root directory holding e1_backends … e5_consent")
	benchAggregateCmd.Flags().String("out-dir", "aggregated", "directory for the generated CSV files")
	benchAggregateCmd.Flags().String("logs-dir", "${FECAD_SHARED:-/srv/fecad}/logs", "directory with SLURM <exp>_<jobid>_<task>.err logs, for incomplete-job reasons")
	benchAggregateCmd.Flags().Int("workers", runtime.NumCPU()*8, "concurrent CSV readers")
	benchCmd.AddCommand(benchAggregateCmd)
}

func runAggregate(resultsDir, outDir, logsDir string, workers int) error {
	if info, err := os.Stat(resultsDir); err != nil || !info.IsDir() {
		return fmt.Errorf("results dir not found: %s", resultsDir)
	}
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}

	jobs := make(chan fileJob, 2048)
	results := make(chan []metricRow, 2048)
	fileCounts := map[string]*int64{}
	for _, s := range experiments {
		var c int64
		fileCounts[s.name] = &c
	}

	var incompleteMu sync.Mutex
	var incomplete []incompleteTask
	var params []taskParams
	// Every task_<N> dir on disk, keyed "<experiment>_<taskID>": separates a
	// not-yet-started squeue task from one already on disk
	seenTasks := map[string]bool{}
	// Finer (experiment, backend, taskID) variant: array indices are reused
	// across backends, so one backend's dir does not prove the other's ran
	seenTaskBackend := map[string]bool{}

	var walkWg sync.WaitGroup
	for _, spec := range experiments {
		walkWg.Add(1)
		go func(s expSpec) {
			defer walkWg.Done()
			walkExperiment(resultsDir, s, jobs, fileCounts[s.name], &incompleteMu, &incomplete, &params, seenTasks, seenTaskBackend)
		}(spec)
	}
	go func() { walkWg.Wait(); close(jobs) }()

	// Parallel: the cost is opening many tiny files
	var readWg sync.WaitGroup
	for i := 0; i < workers; i++ {
		readWg.Add(1)
		go func() {
			defer readWg.Done()
			for j := range jobs {
				if rows := readMetricFile(j); len(rows) > 0 {
					results <- rows
				}
			}
		}()
	}
	go func() { readWg.Wait(); close(results) }()

	byExp := map[string][]metricRow{}
	for rows := range results {
		exp := rows[0].experiment
		byExp[exp] = append(byExp[exp], rows...)
	}

	for _, spec := range experiments {
		rows := byExp[spec.name]
		path := filepath.Join(outDir, spec.name+".csv")
		if err := writeExperimentCSV(path, spec, rows); err != nil {
			return fmt.Errorf("write %s: %w", path, err)
		}
		fmt.Printf("%s: %d files, %d metric rows -> %s\n",
			spec.name, atomic.LoadInt64(fileCounts[spec.name]), len(rows), path)
	}

	aggPath := filepath.Join(outDir, "aggregate.csv")
	nGroups, err := writeAggregateCSV(aggPath, byExp)
	if err != nil {
		return fmt.Errorf("write %s: %w", aggPath, err)
	}
	fmt.Printf("aggregate: %d groups -> %s\n", nGroups, aggPath)

	paramsPath := filepath.Join(outDir, "params.csv")
	nParams, err := writeParamsCSV(paramsPath, params)
	if err != nil {
		return fmt.Errorf("write %s: %w", paramsPath, err)
	}
	fmt.Printf("params: %d tasks -> %s\n", nParams, paramsPath)

	incPath := filepath.Join(outDir, "incomplete.csv")
	nInc, err := writeIncompleteCSV(incPath, incomplete, byExp, logsDir, seenTasks, seenTaskBackend)
	if err != nil {
		return fmt.Errorf("write %s: %w", incPath, err)
	}
	fmt.Printf("incomplete: %d tasks -> %s\n", nInc, incPath)
	return nil
}

func walkExperiment(resultsDir string, spec expSpec, jobs chan<- fileJob, count *int64,
	incMu *sync.Mutex, incList *[]incompleteTask, params *[]taskParams, seen, seenBackend map[string]bool) {
	base := filepath.Join(resultsDir, spec.subdir)
	ndim := len(spec.dims)
	want := ndim + 4 // backend / dims… / task_N / entity / performance_metrics.csv
	_ = filepath.WalkDir(base, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if strings.HasPrefix(d.Name(), "task_") {
				checkTaskDir(base, path, spec, incMu, incList, params, seen, seenBackend)
			}
			return nil
		}
		if d.Name() != "performance_metrics.csv" {
			return nil
		}
		rel, err := filepath.Rel(base, path)
		if err != nil {
			return nil
		}
		parts := strings.Split(rel, string(filepath.Separator))
		if len(parts) != want {
			return nil
		}
		backend := parts[0]
		if !knownBackends[backend] {
			return nil
		}
		taskPart := parts[1+ndim]
		if !strings.HasPrefix(taskPart, "task_") {
			return nil
		}
		taskID, err := strconv.Atoi(strings.TrimPrefix(taskPart, "task_"))
		if err != nil {
			return nil
		}
		dimVals := slices.Clone(parts[1 : 1+ndim])
		atomic.AddInt64(count, 1)
		jobs <- fileJob{
			path:    path,
			spec:    spec,
			backend: backend,
			dimVals: dimVals,
			taskID:  taskID,
			entity:  parts[2+ndim],
		}
		return nil
	})
}

// classify one task_<N>
// marker present → params, absent → incomplete. seen and seenBackend record
// every dir found so squeue and ghost detection can tell missing from unstarted
func checkTaskDir(base, path string, spec expSpec, incMu *sync.Mutex, incList *[]incompleteTask, params *[]taskParams, seen, seenBackend map[string]bool) {
	ndim := len(spec.dims)
	rel, err := filepath.Rel(base, path)
	if err != nil {
		return
	}
	parts := strings.Split(rel, string(filepath.Separator))
	if len(parts) != ndim+2 { // backend / dims… / task_N
		return
	}
	backend := parts[0]
	if !knownBackends[backend] {
		return
	}
	taskID, err := strconv.Atoi(strings.TrimPrefix(parts[ndim+1], "task_"))
	if err != nil {
		return
	}
	complete := false
	var rec completionRecord
	if raw, err := os.ReadFile(filepath.Join(path, "complete.json")); err == nil {
		complete = true
		_ = json.Unmarshal(raw, &rec)
	}
	incMu.Lock()
	seen[runningKey(spec.name, taskID)] = true
	seenBackend[ghostKey(spec.name, backend, taskID)] = true
	if complete {
		*params = append(*params, taskParams{
			experiment: spec.name,
			backend:    backend,
			taskID:     taskID,
			rec:        rec,
		})
	} else {
		*incList = append(*incList, incompleteTask{
			experiment: spec.name,
			backend:    backend,
			dimVals:    slices.Clone(parts[1 : 1+ndim]),
			taskID:     taskID,
		})
	}
	incMu.Unlock()
}

func readMetricFile(j fileJob) []metricRow {
	f, err := os.Open(j.path)
	if err != nil {
		return nil
	}
	defer f.Close()

	r := csv.NewReader(f)
	r.FieldsPerRecord = -1
	recs, err := r.ReadAll()
	if err != nil || len(recs) < 2 {
		return nil
	}
	col := map[string]int{}
	for i, name := range recs[0] {
		col[name] = i
	}
	get := func(rec []string, name string) string {
		if i, ok := col[name]; ok && i < len(rec) {
			return rec[i]
		}
		return ""
	}

	entityType := j.entity
	switch {
	case strings.HasPrefix(j.entity, "provider"):
		entityType = "provider"
	case strings.HasPrefix(j.entity, "patient"):
		entityType = "patient"
	}
	d := assignDims(j.spec, j.dimVals)

	// back-compat: old CSVs carry network_bytes instead of net_out/net_in
	_, hasNetOut := col["net_out_bytes"]

	out := make([]metricRow, 0, len(recs)-1)
	for _, rec := range recs[1:] {
		if len(rec) == 0 {
			continue
		}
		dur, _ := strconv.ParseFloat(get(rec, "duration_sec"), 64)
		var netOut, netIn int64
		if hasNetOut {
			netOut, _ = strconv.ParseInt(get(rec, "net_out_bytes"), 10, 64)
			netIn, _ = strconv.ParseInt(get(rec, "net_in_bytes"), 10, 64)
		} else {
			netOut, _ = strconv.ParseInt(get(rec, "network_bytes"), 10, 64)
		}
		artifactSz, _ := strconv.ParseInt(get(rec, "artifact_size_bytes"), 10, 64)
		ram, _ := strconv.ParseFloat(get(rec, "peak_ram_mb"), 64)
		out = append(out, metricRow{
			experiment:  j.spec.name,
			backend:     j.backend,
			query:       d.query,
			records:     d.records,
			nProviders:  d.nProviders,
			dataset:     d.dataset,
			consentFrac: d.consentFrac,
			storage:     d.storage,
			wanProfile:  d.wanProfile,
			taskID:      j.taskID,
			entity:      j.entity,
			entityType:  entityType,
			timestamp:   get(rec, "timestamp"),
			phase:       get(rec, "phase"),
			durationSec: dur,
			status:      get(rec, "status"),
			netOut:      netOut,
			netIn:       netIn,
			artifactSz:  artifactSz,
			peakRAMMB:   ram,
		})
	}
	return out
}

// dimensions from a result path
type dimVals struct {
	query, records, nProviders, dataset, consentFrac, storage, wanProfile string
}

// ordered dims to named fields
// e5's frac_tag "0_25" is normalised to "0.25"
func assignDims(spec expSpec, vals []string) dimVals {
	var d dimVals
	for i, name := range spec.dims {
		if i >= len(vals) {
			break
		}
		v := vals[i]
		switch name {
		case "query":
			d.query = v
		case "records":
			d.records = v
		case "n_providers":
			d.nProviders = v
		case "dataset":
			d.dataset = v
		case "consent_fraction":
			d.consentFrac = strings.ReplaceAll(v, "_", ".")
		case "storage":
			d.storage = v
		case "wan_profile":
			d.wanProfile = v
		}
	}
	return d
}

func writeExperimentCSV(path string, spec expSpec, rows []metricRow) error {
	sortMetricRows(spec, rows)

	header := append([]string{"backend"}, spec.dims...)
	header = append(header, "task_id", "entity", "entity_type",
		"timestamp", "phase", "duration_sec", "status",
		"net_out_bytes", "net_in_bytes", "artifact_size_bytes", "peak_ram_mb")

	records := make([][]string, 0, len(rows))
	for _, r := range rows {
		rec := []string{r.backend}
		for _, d := range spec.dims {
			rec = append(rec, r.dim(d))
		}
		rec = append(rec,
			strconv.Itoa(r.taskID), r.entity, r.entityType,
			r.timestamp, r.phase, strconv.FormatFloat(r.durationSec, 'f', 4, 64),
			r.status, strconv.FormatInt(r.netOut, 10), strconv.FormatInt(r.netIn, 10),
			strconv.FormatInt(r.artifactSz, 10),
			strconv.FormatFloat(r.peakRAMMB, 'f', 2, 64))
		records = append(records, rec)
	}
	return writeCSV(path, header, records)
}

func sortMetricRows(spec expSpec, rows []metricRow) {
	sort.SliceStable(rows, func(i, j int) bool {
		a, b := rows[i], rows[j]
		if a.backend != b.backend {
			return a.backend < b.backend
		}
		for _, d := range spec.dims {
			if c := cmpNumAware(a.dim(d), b.dim(d)); c != 0 {
				return c < 0
			}
		}
		if a.taskID != b.taskID {
			return a.taskID < b.taskID
		}
		if a.entity != b.entity {
			return a.entity < b.entity
		}
		return a.timestamp < b.timestamp
	})
}

type aggKey struct {
	experiment, backend, query, records, nProviders, dataset, consentFrac string
	storage, wanProfile                                                   string
	entityType, phase                                                     string
}

type aggStat struct {
	durs           []float64 // kept for std + quartiles
	netOutSum      float64
	netOutMax      int64
	netInSum       float64
	netInMax       int64
	artifactSum    float64
	artifactMax    int64
	ramSum, ramMax float64
}

func writeAggregateCSV(path string, byExp map[string][]metricRow) (int, error) {
	stats := map[aggKey]*aggStat{}
	for _, spec := range experiments {
		for _, r := range byExp[spec.name] {
			k := aggKey{
				experiment: r.experiment, backend: r.backend,
				query: r.query, records: r.records, nProviders: r.nProviders,
				dataset: r.dataset, consentFrac: r.consentFrac,
				storage: r.storage, wanProfile: r.wanProfile,
				entityType: r.entityType, phase: r.phase,
			}
			s := stats[k]
			if s == nil {
				s = &aggStat{}
				stats[k] = s
			}
			s.durs = append(s.durs, r.durationSec)
			s.netOutSum += float64(r.netOut)
			if r.netOut > s.netOutMax {
				s.netOutMax = r.netOut
			}
			s.netInSum += float64(r.netIn)
			if r.netIn > s.netInMax {
				s.netInMax = r.netIn
			}
			s.artifactSum += float64(r.artifactSz)
			if r.artifactSz > s.artifactMax {
				s.artifactMax = r.artifactSz
			}
			s.ramSum += r.peakRAMMB
			s.ramMax = math.Max(s.ramMax, r.peakRAMMB)
		}
	}

	keys := make([]aggKey, 0, len(stats))
	for k := range stats {
		keys = append(keys, k)
	}
	expRank := map[string]int{}
	for i, s := range experiments {
		expRank[s.name] = i
	}
	sort.Slice(keys, func(i, j int) bool {
		a, b := keys[i], keys[j]
		if a.experiment != b.experiment {
			return expRank[a.experiment] < expRank[b.experiment]
		}
		if a.backend != b.backend {
			return a.backend < b.backend
		}
		for _, pair := range [][2]string{
			{a.query, b.query}, {a.records, b.records}, {a.nProviders, b.nProviders},
			{a.dataset, b.dataset}, {a.consentFrac, b.consentFrac},
			{a.storage, b.storage}, {a.wanProfile, b.wanProfile},
		} {
			if c := cmpNumAware(pair[0], pair[1]); c != 0 {
				return c < 0
			}
		}
		if a.entityType != b.entityType {
			return a.entityType < b.entityType
		}
		return a.phase < b.phase
	})

	header := []string{
		"experiment", "backend", "query", "records", "n_providers", "dataset",
		"consent_fraction", "storage", "wan_profile", "entity_type", "phase", "n",
		"duration_mean", "duration_std", "duration_min", "duration_max",
		"duration_q25", "duration_median", "duration_q75",
		"net_out_bytes_mean", "net_out_bytes_max",
		"net_in_bytes_mean", "net_in_bytes_max",
		"artifact_size_bytes_mean", "artifact_size_bytes_max",
		"peak_ram_mb_mean", "peak_ram_mb_max",
	}
	records := make([][]string, 0, len(keys))
	for _, k := range keys {
		s := stats[k]
		n := len(s.durs)
		slices.Sort(s.durs) // quantile and min/max need sorted input
		var sum, sumSq float64
		for _, d := range s.durs {
			sum += d
			sumSq += d * d
		}
		mean := sum / float64(n)
		records = append(records, []string{
			k.experiment, k.backend, k.query, k.records, k.nProviders, k.dataset,
			k.consentFrac, k.storage, k.wanProfile, k.entityType, k.phase, strconv.Itoa(n),
			strconv.FormatFloat(mean, 'f', 6, 64),
			strconv.FormatFloat(sampleStd(sum, sumSq, n), 'f', 6, 64),
			strconv.FormatFloat(s.durs[0], 'f', 6, 64),
			strconv.FormatFloat(s.durs[n-1], 'f', 6, 64),
			strconv.FormatFloat(quantile(s.durs, 0.25), 'f', 6, 64),
			strconv.FormatFloat(quantile(s.durs, 0.50), 'f', 6, 64),
			strconv.FormatFloat(quantile(s.durs, 0.75), 'f', 6, 64),
			strconv.FormatFloat(s.netOutSum/float64(n), 'f', 1, 64),
			strconv.FormatInt(s.netOutMax, 10),
			strconv.FormatFloat(s.netInSum/float64(n), 'f', 1, 64),
			strconv.FormatInt(s.netInMax, 10),
			strconv.FormatFloat(s.artifactSum/float64(n), 'f', 1, 64),
			strconv.FormatInt(s.artifactMax, 10),
			strconv.FormatFloat(s.ramSum/float64(n), 'f', 2, 64),
			strconv.FormatFloat(s.ramMax, 'f', 2, 64),
		})
	}
	return len(records), writeCSV(path, header, records)
}

// one row per completed task
// column names match aggregate.csv so plots.ipynb can filter both the same way
func writeParamsCSV(path string, params []taskParams) (int, error) {
	expRank := map[string]int{}
	for i, s := range experiments {
		expRank[s.name] = i
	}
	sort.Slice(params, func(i, j int) bool {
		a, b := params[i], params[j]
		if a.experiment != b.experiment {
			return expRank[a.experiment] < expRank[b.experiment]
		}
		if a.backend != b.backend {
			return a.backend < b.backend
		}
		return a.taskID < b.taskID
	})

	header := []string{
		"experiment", "task_id", "backend", "dataset", "query",
		"records", "n_providers", "consent_fraction",
		"schema_file", "data_file", "build_time",
	}
	records := make([][]string, 0, len(params))
	for _, p := range params {
		r := p.rec
		backend := r.Backend
		if backend == "" {
			backend = p.backend // pre-build_time markers omit it; path has it
		}
		records = append(records, []string{
			p.experiment, strconv.Itoa(p.taskID), backend,
			r.Dataset, r.Query,
			strconv.Itoa(r.Records), strconv.Itoa(r.NumProviders),
			strconv.FormatFloat(r.ConsentFraction, 'f', -1, 64),
			r.SchemaFile, r.DataFile, r.BuildTime,
		})
	}
	return len(records), writeCSV(path, header, records)
}

// one benchmark task
type taskKey struct {
	experiment, backend                              string
	query, records, nProviders, dataset, consentFrac string
	taskID                                           int
}

// report unfinished tasks
// statuses are as described in the file header; seen is keyed (exp, taskID),
// seenBackend (exp, backend, taskID) to suppress ghosts for on-disk backends
func writeIncompleteCSV(path string, tasks []incompleteTask, byExp map[string][]metricRow, logsDir string, seen, seenBackend map[string]bool) (int, error) {
	hasMetrics := map[taskKey]bool{}
	for _, spec := range experiments {
		for _, r := range byExp[spec.name] {
			hasMetrics[taskKey{
				experiment: r.experiment, backend: r.backend,
				query: r.query, records: r.records, nProviders: r.nProviders,
				dataset: r.dataset, consentFrac: r.consentFrac, taskID: r.taskID,
			}] = true
		}
	}

	specByName := map[string]expSpec{}
	expRank := map[string]int{}
	for i, s := range experiments {
		specByName[s.name] = s
		expRank[s.name] = i
	}

	type incRow struct {
		key    taskKey
		status string
	}
	rows := make([]incRow, 0, len(tasks))
	for _, t := range tasks {
		d := assignDims(specByName[t.experiment], t.dimVals)
		k := taskKey{
			experiment: t.experiment, backend: t.backend,
			query: d.query, records: d.records, nProviders: d.nProviders,
			dataset: d.dataset, consentFrac: d.consentFrac, taskID: t.taskID,
		}
		status := "no-output"
		if hasMetrics[k] {
			status = "partial"
		}
		rows = append(rows, incRow{key: k, status: status})
	}

	running := loadRunningTasks()
	// squeue tasks with no task_<N> dir yet: runTask creates the persist dir only
	// at end-of-task, so the disk walk cannot see a still-running task
	for rk := range running {
		if seen[rk] {
			continue
		}
		exp, taskID, ok := parseRunningKey(rk)
		if !ok {
			continue
		}
		rows = append(rows, incRow{
			key:    taskKey{experiment: exp, taskID: taskID},
			status: "in-progress",
		})
	}

	// Ghost rows: a log whose (exp, backend, taskID) never produced a task dir —
	// an ENOSPC kill leaves no on-disk trace. Skipped when squeue lists the task,
	// since a fresh re-submission supersedes the old log
	logIndex := buildLogIndex(logsDir)
	if logIndex != nil {
		for expTask, errPaths := range logIndex {
			exp, taskID, ok := parseRunningKey(expTask)
			if !ok {
				continue
			}
			if _, r := running[expTask]; r {
				continue
			}
			newestByBackend := map[string]string{} // backend -> newest .err path
			for _, p := range errPaths {           // already newest-first
				b := logBackend(p)
				if b == "" {
					continue
				}
				if _, dup := newestByBackend[b]; !dup {
					newestByBackend[b] = p
				}
			}
			for backend, errPath := range newestByBackend {
				if seenBackend[ghostKey(exp, backend, taskID)] {
					continue
				}
				h := parseLogHeader(strings.TrimSuffix(errPath, ".err") + ".out")
				rows = append(rows, incRow{
					key: taskKey{
						experiment: exp, backend: backend,
						query:       h["query"],
						records:     h["records"],
						nProviders:  h["n_providers"],
						dataset:     h["dataset"],
						consentFrac: h["consent_fraction"],
						taskID:      taskID,
					},
					status: "ghost",
				})
			}
		}
	}

	sort.Slice(rows, func(i, j int) bool {
		a, b := rows[i].key, rows[j].key
		if a.experiment != b.experiment {
			return expRank[a.experiment] < expRank[b.experiment]
		}
		if a.backend != b.backend {
			return a.backend < b.backend
		}
		for _, pair := range [][2]string{
			{a.query, b.query}, {a.records, b.records}, {a.nProviders, b.nProviders},
			{a.dataset, b.dataset}, {a.consentFrac, b.consentFrac},
		} {
			if c := cmpNumAware(pair[0], pair[1]); c != 0 {
				return c < 0
			}
		}
		return a.taskID < b.taskID
	})

	keys := make([]taskKey, len(rows))
	for i, r := range rows {
		keys[i] = r.key
	}
	reasons := resolveReasons(logsDir, keys)

	header := []string{
		"experiment", "backend", "query", "records", "n_providers",
		"dataset", "consent_fraction", "task_id", "status", "failure", "reason",
	}
	records := make([][]string, 0, len(rows))
	for i, r := range rows {
		k := r.key
		failure, reason := reasons[i].failure, reasons[i].reason
		if state, ok := running[runningKey(k.experiment, k.taskID)]; ok {
			failure, reason = squeueFailure(state), "squeue: "+state
		}
		records = append(records, []string{
			k.experiment, k.backend, k.query, k.records, k.nProviders,
			k.dataset, k.consentFrac, strconv.Itoa(k.taskID), r.status,
			failure, reason,
		})
	}
	return len(records), writeCSV(path, header, records)
}

func runningKey(experiment string, taskID int) string {
	return experiment + "_" + strconv.Itoa(taskID)
}

// inverse of runningKey
func parseRunningKey(key string) (experiment string, taskID int, ok bool) {
	i := strings.LastIndexByte(key, '_')
	if i < 0 {
		return "", 0, false
	}
	id, err := strconv.Atoi(key[i+1:])
	if err != nil {
		return "", 0, false
	}
	return key[:i], id, true
}

// one (experiment, backend, taskID) attempt
// distinct from runningKey: array indices are reused across backends
func ghostKey(experiment, backend string, taskID int) string {
	return experiment + "_" + backend + "_" + strconv.Itoa(taskID)
}

// key=value tokens on a .out first line
// e.g. "E2 task 107: backend=lattigo records=50000 query=q4 rep=2"
var headerKVRe = regexp.MustCompile(`(\w+)=(\S+)`)

// ghost-row dims from a .out
// e5's consent_fraction is already decimal there, so no normalisation
func parseLogHeader(outPath string) map[string]string {
	data, err := os.ReadFile(outPath)
	if err != nil {
		return nil
	}
	line := string(data)
	if nl := strings.IndexByte(line, '\n'); nl >= 0 {
		line = line[:nl]
	}
	m := map[string]string{}
	for _, kv := range headerKVRe.FindAllStringSubmatch(line, -1) {
		m[kv[1]] = kv[2]
	}
	return m
}

// SLURM state to failure column
func squeueFailure(state string) string {
	switch state {
	case "PENDING", "CONFIGURING", "REQUEUED", "RESV_DEL_HOLD", "REQUEUE_FED",
		"REQUEUE_HOLD", "SPECIAL_EXIT":
		return "pending"
	default:
		return "running"
	}
}

// squeue array tasks by "<exp>_<taskID>"
// nil when squeue is missing, so the caller skips the override; job names must
// match an experiment's subdir
func loadRunningTasks() map[string]string {
	out, err := exec.Command("squeue", "--me", "-h", "-r", "-o", "%j|%K|%T").Output()
	if err != nil {
		return nil
	}
	nameToExp := map[string]string{}
	for _, s := range experiments {
		nameToExp[s.subdir] = s.name
	}
	running := map[string]string{}
	for _, raw := range strings.Split(string(out), "\n") {
		line := strings.TrimSpace(raw)
		if line == "" {
			continue
		}
		parts := strings.Split(line, "|")
		if len(parts) != 3 {
			continue
		}
		name, taskStr, state := parts[0], parts[1], parts[2]
		exp, ok := nameToExp[name]
		if !ok {
			continue
		}
		if _, err := strconv.Atoi(taskStr); err != nil {
			continue
		}
		running[exp+"_"+taskStr] = state
	}
	return running
}

// failure category and last stderr line
type reasonInfo struct {
	failure string
	reason  string
}

// <experiment>_<jobid>_<task>.err
var logNameRe = regexp.MustCompile(`^(e[1-9])_(\d+)_(\d+)\.err$`)

// failure reason per task
// parallel because BeeGFS small-file opens are latency-bound
func resolveReasons(logsDir string, keys []taskKey) []reasonInfo {
	out := make([]reasonInfo, len(keys))
	index := buildLogIndex(logsDir)
	if index == nil {
		return out
	}
	work := make(chan int, len(keys))
	for i := range keys {
		work <- i
	}
	close(work)
	var wg sync.WaitGroup
	for w := 0; w < runtime.NumCPU()*4; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range work {
				out[i] = reasonForTask(index, keys[i])
			}
		}()
	}
	wg.Wait()
	return out
}

// reason from the newest matching log
// the array index alone is ambiguous across backends, so the .out's recorded
// backend is checked; "orphaned" when no log ran this backend at this index
func reasonForTask(index map[string][]string, k taskKey) reasonInfo {
	for _, errPath := range index[k.experiment+"_"+strconv.Itoa(k.taskID)] {
		if logBackend(errPath) == k.backend {
			return extractReason(errPath)
		}
	}
	return reasonInfo{failure: "orphaned"}
}

// "<exp>_<task>" to .err paths, newest first
func buildLogIndex(logsDir string) map[string][]string {
	entries, err := os.ReadDir(logsDir)
	if err != nil {
		return nil
	}
	type cand struct {
		jobID int
		path  string
	}
	cands := map[string][]cand{}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		m := logNameRe.FindStringSubmatch(e.Name())
		if m == nil {
			continue
		}
		jobID, _ := strconv.Atoi(m[2])
		key := m[1] + "_" + m[3]
		cands[key] = append(cands[key], cand{jobID: jobID, path: filepath.Join(logsDir, e.Name())})
	}
	index := make(map[string][]string, len(cands))
	for key, cs := range cands {
		slices.SortFunc(cs, func(a, b cand) int { return cmp.Compare(b.jobID, a.jobID) })
		paths := make([]string, len(cs))
		for i, c := range cs {
			paths[i] = c.path
		}
		index[key] = paths
	}
	return index
}

// backend from the .out sibling
// e.g. "E1 task 9: backend=lattigo rep=9"; "" when missing or unparseable
func logBackend(errPath string) string {
	data, err := os.ReadFile(strings.TrimSuffix(errPath, ".err") + ".out")
	if err != nil {
		return ""
	}
	line := string(data)
	if nl := strings.IndexByte(line, '\n'); nl >= 0 {
		line = line[:nl]
	}
	i := strings.Index(line, "backend=")
	if i < 0 {
		return ""
	}
	field := line[i+len("backend="):]
	if sp := strings.IndexAny(field, " \t"); sp >= 0 {
		field = field[:sp]
	}
	return field
}

// classify a .err log tail
// the error is always in the final lines, so only the tail is read
func extractReason(logPath string) reasonInfo {
	const tailBytes = 32 * 1024
	f, err := os.Open(logPath)
	if err != nil {
		return reasonInfo{failure: "no-log"}
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return reasonInfo{failure: "no-log"}
	}
	off := int64(0)
	if info.Size() > tailBytes {
		off = info.Size() - tailBytes
	}
	buf := make([]byte, info.Size()-off)
	if _, err := f.ReadAt(buf, off); err != nil && err != io.EOF {
		return reasonInfo{failure: "no-log"}
	}
	tail := string(buf)

	last := ""
	for _, line := range strings.Split(tail, "\n") {
		if s := strings.TrimSpace(line); s != "" {
			last = s
		}
	}
	return reasonInfo{failure: classifyFailure(tail, last), reason: truncateReason(last)}
}

// bucket a stderr tail
// SLURM kill notices can sit above the last line, so the whole tail is scanned
func classifyFailure(tail, lastLine string) string {
	lower := strings.ToLower(tail)
	switch {
	case strings.Contains(lastLine, `"msg":"task complete"`):
		// run-task logged success; the marker was lost afterwards
		return "completed"
	case strings.Contains(lower, "due to time limit"):
		return "timeout"
	case strings.Contains(lower, "oom-kill"),
		strings.Contains(lower, "out of memory"),
		strings.Contains(lower, "out-of-memory"):
		return "oom"
	case strings.Contains(lower, "no space left on device"):
		return "disk"
	case strings.Contains(lower, "cancelled"):
		return "cancelled"
	case lastLine == "":
		return "empty-log"
	default:
		return "error"
	}
}

func truncateReason(s string) string {
	const max = 300
	if len(s) <= max {
		return s
	}
	return s[:max] + "…"
}

// quantile of a sorted slice
// linear interpolation, matching numpy.percentile and pandas .quantile
func quantile(sorted []float64, p float64) float64 {
	n := len(sorted)
	if n == 0 {
		return 0
	}
	if n == 1 {
		return sorted[0]
	}
	rank := p * float64(n-1)
	lo := int(math.Floor(rank))
	hi := int(math.Ceil(rank))
	return sorted[lo] + (rank-float64(lo))*(sorted[hi]-sorted[lo])
}

// ddof=1 std, matching pandas .std
func sampleStd(sum, sumSq float64, n int) float64 {
	if n < 2 {
		return 0
	}
	variance := (sumSq - sum*sum/float64(n)) / float64(n-1)
	if variance < 0 {
		variance = 0
	}
	return math.Sqrt(variance)
}

// numeric-aware string compare
// empty sorts first
func cmpNumAware(a, b string) int {
	if a == b {
		return 0
	}
	if a == "" {
		return -1
	}
	if b == "" {
		return 1
	}
	fa, ea := strconv.ParseFloat(a, 64)
	fb, eb := strconv.ParseFloat(b, 64)
	if ea == nil && eb == nil {
		switch {
		case fa < fb:
			return -1
		case fa > fb:
			return 1
		default:
			return 0
		}
	}
	if a < b {
		return -1
	}
	return 1
}

func writeCSV(path string, header []string, records [][]string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	w := csv.NewWriter(f)
	werr := w.Write(header)
	if werr == nil {
		werr = w.WriteAll(records)
	}
	w.Flush()
	if werr == nil {
		werr = w.Error()
	}
	// fsync before close: BeeGFS can otherwise drop buffered writes silently
	if werr == nil {
		werr = f.Sync()
	}
	if cerr := f.Close(); werr == nil {
		werr = cerr
	}
	return werr
}
