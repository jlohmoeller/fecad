// Package metrics writes performance CSV rows
//
//	timestamp,phase,duration_sec,task,dataset,query,status,net_out_bytes,net_in_bytes,artifact_size_bytes,peak_ram_mb
package metrics

import (
	"encoding/csv"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/fecad/internal/sysinfo"
)

// per-phase benchmark context
type Meta struct {
	Task    string // numeric task index, e.g. "0"
	Dataset string // e.g. "nuclear_medicine"
	Query   string // e.g. "q1"
	Status  string // e.g. "success", a provider ID, or other context
}

// split "task{N}_{dataset}_{query}"
func ParseRunID(runID string) Meta {
	s := strings.TrimPrefix(runID, "task")
	before, after, ok := strings.Cut(s, "_")
	if !ok {
		return Meta{Task: s}
	}
	task := before
	rest := after
	j := strings.LastIndex(rest, "_")
	if j < 0 {
		return Meta{Task: task, Dataset: rest}
	}
	return Meta{Task: task, Dataset: rest[:j], Query: rest[j+1:]}
}

// appends to dataDir/performance_metrics.csv
type Writer struct {
	mu   sync.Mutex
	path string
}

func NewWriter(dataDir string) *Writer {
	return &Writer{path: filepath.Join(dataDir, "performance_metrics.csv")}
}

// one row, no artifact size
func (w *Writer) Record(phase string, duration time.Duration, meta Meta, netOut, netIn int64) {
	w.RecordSized(phase, duration, meta, netOut, netIn, 0)
}

// one row with artifact size
func (w *Writer) RecordSized(phase string, duration time.Duration, meta Meta, netOut, netIn, artifactSize int64) {
	w.mu.Lock()
	defer w.mu.Unlock()

	needsHeader := !fileExists(w.path)
	f, err := os.OpenFile(w.path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return
	}
	defer f.Close()

	cw := csv.NewWriter(f)
	if needsHeader {
		_ = cw.Write([]string{"timestamp", "phase", "duration_sec", "task", "dataset", "query", "status", "net_out_bytes", "net_in_bytes", "artifact_size_bytes", "peak_ram_mb"})
	}

	peakRAM := sysinfo.PeakRAMMB() + sysinfo.ChildrenPeakRAMMB()
	_ = cw.Write([]string{
		time.Now().Format("2006-01-02T15:04:05.999999"),
		phase,
		fmt.Sprintf("%.4f", duration.Seconds()),
		meta.Task,
		meta.Dataset,
		meta.Query,
		meta.Status,
		fmt.Sprintf("%d", netOut),
		fmt.Sprintf("%d", netIn),
		fmt.Sprintf("%d", artifactSize),
		fmt.Sprintf("%.2f", peakRAM),
	})
	cw.Flush()
}

// deferred timing helper
//
//	defer m.Time("my-phase", meta, 0, 0)()
func (w *Writer) Time(phase string, meta Meta, netOut, netIn int64) func() {
	start := time.Now()
	return func() { w.Record(phase, time.Since(start), meta, netOut, netIn) }
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
