package cmd

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"go.uber.org/zap"

	"github.com/fecad/internal/metrics"
	"github.com/fecad/internal/schema"
)

// one parsed condition
// sqlparser format "colIdx,scaledVal,OP,LOGIC,maxVal", LOGIC "NONE" on the first
type parsedCond struct {
	colIdx int
	val    int64
	op     string
	logic  string
}

func parseInstructions(instructions string) ([]parsedCond, error) {
	if instructions == "" {
		return nil, nil
	}
	var out []parsedCond
	for _, raw := range strings.Split(instructions, ";") {
		f := strings.Split(raw, ",")
		if len(f) < 4 {
			return nil, fmt.Errorf("bad instruction %q", raw)
		}
		ci, err := strconv.Atoi(f[0])
		if err != nil {
			return nil, fmt.Errorf("bad colIdx in %q: %w", raw, err)
		}
		v, err := strconv.ParseInt(f[1], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("bad val in %q: %w", raw, err)
		}
		out = append(out, parsedCond{colIdx: ci, val: v, op: f[2], logic: f[3]})
	}
	return out, nil
}

func rowInt64(row map[string]any, col string) (int64, bool) {
	v, ok := row[col]
	if !ok {
		return 0, false
	}
	switch x := v.(type) {
	case float64:
		return int64(x), true
	case int64:
		return x, true
	case int:
		return int64(x), true
	case json.Number:
		i, err := x.Int64()
		if err != nil {
			return 0, false
		}
		return i, true
	}
	return 0, false
}

func cmpOp(op string, lhs, rhs int64) bool {
	switch op {
	case "EQ":
		return lhs == rhs
	case "LT":
		return lhs < rhs
	case "LE":
		return lhs <= rhs
	case "GT":
		return lhs > rhs
	case "GE":
		return lhs >= rhs
	}
	return false
}

// fold conds in plaintext
// SQL precedence: AND tighter than OR, matching every backend's evalConds
func evalPredicate(row map[string]any, sc *schema.Schema, conds []parsedCond) bool {
	if len(conds) == 0 {
		return true
	}
	var (
		group, result       bool
		hasGroup, hasResult bool
	)
	for _, c := range conds {
		col := sc.Columns[c.colIdx]
		lhs, ok := rowInt64(row, col.Name)
		if !ok {
			return false
		}
		cur := cmpOp(c.op, lhs, c.val)
		if !hasGroup {
			group, hasGroup = cur, true
			continue
		}
		switch c.logic {
		case "OR":
			if hasResult {
				result = result || group
			} else {
				result, hasResult = group, true
			}
			group = cur
		default: // AND or unknown → AND
			group = group && cur
		}
	}
	if hasGroup {
		if hasResult {
			result = result || group
		} else {
			result = group
		}
	}
	return result
}

// decrypted bits, flattened
// accepts nested [[v0,…],…] (lattigo, null) and flat [v0,…] (he3db/cgo)
func parseDecryptedBits(data []byte, rows int) ([]uint64, error) {
	var bits []uint64
	var nested [][]uint64
	if err := json.Unmarshal(data, &nested); err == nil {
		for _, chunk := range nested {
			bits = append(bits, chunk...)
		}
	} else if err := json.Unmarshal(data, &bits); err != nil {
		return nil, fmt.Errorf("decode bits: %w", err)
	}
	if len(bits) > rows {
		bits = bits[:rows]
	}
	return bits, nil
}

// confusion matrix tallies
type counts struct {
	TP, FP, TN, FN int
}

func (c counts) total() int { return c.TP + c.FP + c.TN + c.FN }

func (c counts) accuracy() float64 {
	t := c.total()
	if t == 0 {
		return 0
	}
	return float64(c.TP+c.TN) / float64(t)
}

func (c counts) precision() float64 {
	d := c.TP + c.FP
	if d == 0 {
		return 0
	}
	return float64(c.TP) / float64(d)
}

func (c counts) recall() float64 {
	d := c.TP + c.FN
	if d == 0 {
		return 0
	}
	return float64(c.TP) / float64(d)
}

// score one provider
// non-consented rows are masked to 0 by design, so they are never scored
func scoreProvider(dataFile string, conds []parsedCond, sc *schema.Schema, bits []uint64) (counts, error) {
	raw, err := os.ReadFile(dataFile)
	if err != nil {
		return counts{}, err
	}
	var rows []map[string]any
	if err := json.Unmarshal(raw, &rows); err != nil {
		return counts{}, err
	}
	var c counts
	n := len(rows)
	if len(bits) < n {
		n = len(bits)
	}
	for i := 0; i < n; i++ {
		consented, _ := rows[i]["consented"].(bool)
		if !consented {
			continue
		}
		gt := evalPredicate(rows[i], sc, conds)
		he := bits[i] == 1
		switch {
		case gt && he:
			c.TP++
		case !gt && he:
			c.FP++
		case gt && !he:
			c.FN++
		default:
			c.TN++
		}
	}
	return c, nil
}

// accuracy scoring
// the breakdown rides in Status so aggregate.go needs no extra CSV column
func scoreAccuracy(
	p runTaskParams,
	taskDir, instructions string,
	sc *schema.Schema,
	decryptedByProvider map[string][]byte,
	runID string,
	rWriter *metrics.Writer,
) {
	conds, err := parseInstructions(instructions)
	if err != nil {
		p.log.Warn("accuracy: parse instructions", zap.Error(err))
		return
	}
	// Aggregated: one federation-wide vector where provider i owns rows
	// [i*records, (i+1)*records). Otherwise one vector per provider
	var aggBits []uint64
	if body, ok := decryptedByProvider[aggregateResultID]; ok {
		var err error
		aggBits, err = parseDecryptedBits(body, p.records*p.numProviders)
		if err != nil {
			p.log.Warn("accuracy: parse aggregate bits", zap.Error(err))
			return
		}
	}

	var total counts
	for i := 0; i < p.numProviders; i++ {
		pid := fmt.Sprintf("provider_%d", i)
		var bits []uint64
		if aggBits != nil {
			lo, hi := i*p.records, (i+1)*p.records
			if hi > len(aggBits) {
				p.log.Warn("accuracy: aggregate vector shorter than expected",
					zap.Int("have", len(aggBits)), zap.Int("want", hi))
				continue
			}
			bits = aggBits[lo:hi]
		} else {
			body, ok := decryptedByProvider[pid]
			if !ok {
				p.log.Warn("accuracy: no decrypted body", zap.String("provider", pid))
				continue
			}
			var err error
			bits, err = parseDecryptedBits(body, p.records)
			if err != nil {
				p.log.Warn("accuracy: parse bits", zap.String("provider", pid), zap.Error(err))
				continue
			}
		}
		dataFile := filepath.Join(taskDir, pid, "data.json")
		c, err := scoreProvider(dataFile, conds, sc, bits)
		if err != nil {
			p.log.Warn("accuracy: score", zap.String("provider", pid), zap.Error(err))
			continue
		}
		total.TP += c.TP
		total.FP += c.FP
		total.TN += c.TN
		total.FN += c.FN
	}
	acc, prec, rec := total.accuracy(), total.precision(), total.recall()
	p.log.Info("accuracy",
		zap.String("entity", "researcher"),
		zap.String("run_id", runID),
		zap.Int("tp", total.TP), zap.Int("fp", total.FP),
		zap.Int("tn", total.TN), zap.Int("fn", total.FN),
		zap.Float64("accuracy", acc),
		zap.Float64("precision", prec),
		zap.Float64("recall", rec))
	aMeta := metrics.ParseRunID(runID)
	aMeta.Status = fmt.Sprintf("acc=%.6f,prec=%.6f,rec=%.6f,tp=%d,fp=%d,tn=%d,fn=%d",
		acc, prec, rec, total.TP, total.FP, total.TN, total.FN)
	rWriter.Record("researcher-accuracy", 0, aMeta, 0, 0)
}
