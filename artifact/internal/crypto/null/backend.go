// Package null implements crypto.Backend in plaintext — no FHE
package null

import (
	"bufio"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"math/big"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"

	"go.uber.org/zap"

	"github.com/fecad/internal/schema"
)

// mirrors the lattigo backend's plaintext prime
const plaintextModulus = uint64(65537)

// mirrors params.MaxSlots()
const nullSlots = 8192

const keyMarker = `"null_key"`

// plaintext backend
type Backend struct{}

func New() *Backend { return &Backend{} }

// column/chunk layout of encrypted_data.bin
type dataMeta struct {
	NCols   int `json:"ncols"`
	NChunks int `json:"nchunks"`
}

// slots and cancellation value for one patient in one chunk
type nullCancEntry struct {
	Chunk int    `json:"c"`
	Slots []int  `json:"s"`
	Value uint64 `json:"v"`
}

// path helpers

func skPath(dir string) string       { return filepath.Join(dir, "secret_key.bin") }
func evkPath(dir string) string      { return filepath.Join(dir, "eval_key.bin") }
func dataCTPath(dir string) string   { return filepath.Join(dir, "encrypted_data.bin") }
func resCTPath(dir string) string    { return filepath.Join(dir, "result_query.bin") }
func dataMetaPath(dir string) string { return filepath.Join(dir, "data_meta.json") }

// blindingsDir is the full subdirectory path
func blindingAggPath(blindingsDir string, chunk int) string {
	return filepath.Join(blindingsDir, fmt.Sprintf("agg_chunk_%d.json", chunk))
}

// cancDir is the full subdirectory path
func cancStorePath(cancDir string) string {
	return filepath.Join(cancDir, "store.json")
}

// CT I/O

func readCTs(path string) ([][]uint64, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("readCTs %s: %w", path, err)
	}
	var cts [][]uint64
	if err := json.Unmarshal(data, &cts); err != nil {
		return nil, fmt.Errorf("readCTs %s: %w", path, err)
	}
	return cts, nil
}

func writeCTs(path string, cts [][]uint64) error {
	data, _ := json.Marshal(cts)
	return os.WriteFile(path, data, 0o644)
}

// dataMeta I/O

func writeDataMeta(dir string, m dataMeta) error {
	b, _ := json.Marshal(m)
	return os.WriteFile(dataMetaPath(dir), b, 0o644)
}

func readDataMeta(dir string) (dataMeta, error) {
	b, err := os.ReadFile(dataMetaPath(dir))
	if err != nil {
		return dataMeta{}, fmt.Errorf("readDataMeta: %w", err)
	}
	var m dataMeta
	return m, json.Unmarshal(b, &m)
}

// blinding aggregate I/O

func loadBlindingAgg(path string) ([]uint64, error) {
	b, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return make([]uint64, nullSlots), nil // zeros → addMod is a no-op
	}
	if err != nil {
		return nil, err
	}
	var agg []uint64
	return agg, json.Unmarshal(b, &agg)
}

func saveBlindingAgg(path string, agg []uint64) error {
	b, _ := json.Marshal(agg)
	return writeFileAtomic(path, b)
}

// Temp file + rename, so a reader never sees a truncated store: the lattigo
// backend raced here and fell back to an unblinded result
func writeFileAtomic(path string, b []byte) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// cancellation store I/O

func loadCancStore(dir string) (map[string][]nullCancEntry, error) {
	b, err := os.ReadFile(cancStorePath(dir))
	if errors.Is(err, os.ErrNotExist) {
		return map[string][]nullCancEntry{}, nil
	}
	if err != nil {
		return nil, err
	}
	var store map[string][]nullCancEntry
	return store, json.Unmarshal(b, &store)
}

func saveCancStore(dir string, store map[string][]nullCancEntry) error {
	b, _ := json.Marshal(store)
	return writeFileAtomic(cancStorePath(dir), b)
}

// key operations

func (b *Backend) GenerateResearcherKey(dataDir string) error {
	zap.L().Info("GenerateResearcherKey", zap.String("backend", "null"), zap.String("dataDir", dataDir))
	return os.WriteFile(skPath(dataDir), []byte(keyMarker), 0o644)
}

func (b *Backend) GenerateProviderKeys(dataDir string) error {
	zap.L().Info("GenerateProviderKeys", zap.String("backend", "null"), zap.String("dataDir", dataDir))
	if err := os.WriteFile(skPath(dataDir), []byte(keyMarker), 0o644); err != nil {
		return err
	}
	return os.WriteFile(evkPath(dataDir), []byte(keyMarker), 0o644)
}

func (b *Backend) GenerateKSK(_, _, kskPath string) error {
	zap.L().Info("GenerateKSK", zap.String("backend", "null"), zap.String("kskPath", kskPath))
	return os.WriteFile(kskPath, []byte(keyMarker), 0o644)
}

func (b *Backend) GenerateKSKQueryForward(_, _, kskPath string) error {
	zap.L().Info("GenerateKSKQueryForward", zap.String("backend", "null"), zap.String("kskPath", kskPath))
	return os.WriteFile(kskPath, []byte(keyMarker), 0o644)
}

func (b *Backend) ApplyKSKToLiterals(_, inPath, outPath string) error {
	zap.L().Info("ApplyKSKToLiterals", zap.String("backend", "null"), zap.String("inPath", inPath), zap.String("outPath", outPath))
	return copyFile(inPath, outPath)
}

func (b *Backend) Aggregate(_, inputPath, outputPath string) error {
	zap.L().Info("Aggregate", zap.String("backend", "null"), zap.String("inputPath", inputPath), zap.String("outputPath", outputPath))
	return copyFile(inputPath, outputPath)
}

// data encryption

// data.json -> encrypted_data.bin + data_meta.json, column-major
func (b *Backend) EncryptData(dataDir, schemaPath string) error {
	zap.L().Info("EncryptData", zap.String("backend", "null"), zap.String("dataDir", dataDir))
	rows, colOrder, err := loadDataJSON(filepath.Join(dataDir, "data.json"))
	if err != nil {
		return fmt.Errorf("EncryptData: %w", err)
	}
	if schemaPath != "" {
		sc, scErr := schema.Load(schemaPath)
		if scErr != nil {
			return fmt.Errorf("EncryptData: load schema: %w", scErr)
		}
		// schema order: instructions carry schema indices
		colOrder = sc.ColumnOrder()
	}
	if len(rows) == 0 {
		return fmt.Errorf("EncryptData: empty dataset")
	}

	nCols := len(colOrder)
	nChunks := (len(rows) + nullSlots - 1) / nullSlots
	cts := make([][]uint64, nCols*nChunks)
	for ci, col := range colOrder {
		for k := 0; k < nChunks; k++ {
			start := k * nullSlots
			end := start + nullSlots
			if end > len(rows) {
				end = len(rows)
			}
			slots := make([]uint64, nullSlots)
			for ri := start; ri < end; ri++ {
				slots[ri-start] = rows[ri][col]
			}
			cts[ci*nChunks+k] = slots
		}
	}
	if err := writeCTs(dataCTPath(dataDir), cts); err != nil {
		return err
	}
	return writeDataMeta(dataDir, dataMeta{NCols: nCols, NChunks: nChunks})
}

// literal encryption

// each value as an all-equal slot vector
func (b *Backend) EncryptLiterals(researcherDir string, values []int64, outPath string) error {
	zap.L().Info("EncryptLiterals", zap.String("backend", "null"), zap.Int("count", len(values)), zap.String("outPath", outPath))
	cts := make([][]uint64, len(values))
	for i, v := range values {
		var uval uint64
		if v >= 0 {
			uval = uint64(v)
		} else {
			uval = plaintextModulus - uint64(-v)%plaintextModulus
		}
		slots := make([]uint64, nullSlots)
		for j := range slots {
			slots[j] = uval
		}
		cts[i] = slots
	}
	return writeCTs(outPath, cts)
}

// predicate evaluation

// predicate -> result_query.bin; slot j of chunk k is 1 iff row k*nullSlots+j matches
func (b *Backend) EvaluatePredicate(dataDir, instructions string) error {
	zap.L().Info("EvaluatePredicate", zap.String("backend", "null"), zap.String("dataDir", dataDir), zap.String("instructions", instructions))
	dataCTs, err := readCTs(dataCTPath(dataDir))
	if err != nil {
		return fmt.Errorf("EvaluatePredicate: load data: %w", err)
	}
	meta, err := readDataMeta(dataDir)
	if err != nil {
		return fmt.Errorf("EvaluatePredicate: %w", err)
	}
	conds := parseInstructions(instructions)
	if len(conds) == 0 {
		return fmt.Errorf("EvaluatePredicate: empty instructions")
	}

	resultCTs := make([][]uint64, meta.NChunks)
	for k := 0; k < meta.NChunks; k++ {
		chunkCTs := make([][]uint64, meta.NCols)
		for ci := 0; ci < meta.NCols; ci++ {
			chunkCTs[ci] = dataCTs[ci*meta.NChunks+k]
		}
		result, err := evalConds(chunkCTs, conds, nil)
		if err != nil {
			return fmt.Errorf("EvaluatePredicate chunk %d: %w", k, err)
		}
		resultCTs[k] = result
	}
	return writeCTs(resCTPath(dataDir), resultCTs)
}

// predicate with literal slot vectors for EQ; ranges use the plaintext value
func (b *Backend) EvaluatePredicateEncLiterals(dataDir, literalsPath, instructions string) error {
	zap.L().Info("EvaluatePredicateEncLiterals", zap.String("backend", "null"), zap.String("dataDir", dataDir), zap.String("literalsPath", literalsPath), zap.String("instructions", instructions))
	dataCTs, err := readCTs(dataCTPath(dataDir))
	if err != nil {
		return fmt.Errorf("EvaluatePredicateEncLiterals: load data: %w", err)
	}
	litCTs, err := readCTs(literalsPath)
	if err != nil {
		return fmt.Errorf("EvaluatePredicateEncLiterals: load literals: %w", err)
	}
	meta, err := readDataMeta(dataDir)
	if err != nil {
		return fmt.Errorf("EvaluatePredicateEncLiterals: %w", err)
	}
	conds := parseInstructions(instructions)
	if len(conds) == 0 {
		return fmt.Errorf("EvaluatePredicateEncLiterals: empty instructions")
	}
	if len(litCTs) < len(conds) {
		return fmt.Errorf("EvaluatePredicateEncLiterals: need %d literal CTs, have %d",
			len(conds), len(litCTs))
	}

	resultCTs := make([][]uint64, meta.NChunks)
	for k := 0; k < meta.NChunks; k++ {
		chunkCTs := make([][]uint64, meta.NCols)
		for ci := 0; ci < meta.NCols; ci++ {
			chunkCTs[ci] = dataCTs[ci*meta.NChunks+k]
		}
		result, err := evalConds(chunkCTs, conds, litCTs)
		if err != nil {
			return fmt.Errorf("EvaluatePredicateEncLiterals chunk %d: %w", k, err)
		}
		resultCTs[k] = result
	}
	return writeCTs(resCTPath(dataDir), resultCTs)
}

// result CTs -> JSON, first rows slots
func (b *Backend) DecryptResult(dataDir, resultPath, outputPath string, rows int) error {
	zap.L().Info("DecryptResult", zap.String("backend", "null"), zap.String("resultPath", resultPath), zap.String("outputPath", outputPath), zap.Int("rows", rows))
	cts, err := readCTs(resultPath)
	if err != nil {
		return fmt.Errorf("DecryptResult: %w", err)
	}
	var results [][]uint64
	remaining := rows
	for _, slots := range cts {
		if remaining <= 0 {
			break
		}
		take := len(slots)
		if take > remaining {
			take = remaining
		}
		results = append(results, slots[:take])
		remaining -= take
	}
	out, _ := json.MarshalIndent(results, "", "  ")
	return os.WriteFile(outputPath, out, 0o644)
}

// blinding / cancellation

// NDJSON append log, one {"p":patientID,"c":chunk,"s":[slots],"v":rpNeg} per line
func cancNDJSONPath(cancDir string) string { return filepath.Join(cancDir, "store.json") }

type nullCancLine struct {
	P string `json:"p"`
	C int    `json:"c"`
	S []int  `json:"s"`
	V uint64 `json:"v"`
}

func loadCancNDJSON(cancDir string) (map[string][]nullCancEntry, error) {
	f, err := os.Open(cancNDJSONPath(cancDir))
	if errors.Is(err, os.ErrNotExist) {
		return map[string][]nullCancEntry{}, nil
	}
	if err != nil {
		return nil, err
	}
	defer f.Close()
	idx := map[string][]nullCancEntry{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Bytes()
		if len(line) == 0 {
			continue
		}
		var e nullCancLine
		if err := json.Unmarshal(line, &e); err != nil {
			return nil, fmt.Errorf("loadCancNDJSON: %w", err)
		}
		idx[e.P] = append(idx[e.P], nullCancEntry{Chunk: e.C, Slots: e.S, Value: e.V})
	}
	return idx, sc.Err()
}

// r_p into agg_chunk_k.json, −r_p appended to the NDJSON store
func (b *Backend) SetupPatientBlinding(providerDir, proxyDir, patientID string, rowIndices []int) error {
	if len(rowIndices) == 0 {
		return nil
	}
	blindDir := filepath.Join(providerDir, "blindings")
	cancDir := filepath.Join(proxyDir, "cancellations")
	if err := os.MkdirAll(blindDir, 0o755); err != nil {
		return err
	}
	if err := os.MkdirAll(cancDir, 0o755); err != nil {
		return err
	}

	rp, err := sampleBlindingValue()
	if err != nil {
		return err
	}
	rpNeg := plaintextModulus - rp

	chunkSlots := map[int][]int{}
	for _, r := range rowIndices {
		chunkSlots[r/nullSlots] = append(chunkSlots[r/nullSlots], r%nullSlots)
	}

	cancF, err := os.OpenFile(cancNDJSONPath(cancDir), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return fmt.Errorf("SetupPatientBlinding: open canc store: %w", err)
	}
	defer cancF.Close()

	for chunk, slots := range chunkSlots {
		agg, err := loadBlindingAgg(blindingAggPath(blindDir, chunk))
		if err != nil {
			return fmt.Errorf("SetupPatientBlinding: load agg chunk %d: %w", chunk, err)
		}
		for _, s := range slots {
			if s >= 0 && s < len(agg) {
				agg[s] = (agg[s] + rp) % plaintextModulus
			}
		}
		if err := saveBlindingAgg(blindingAggPath(blindDir, chunk), agg); err != nil {
			return fmt.Errorf("SetupPatientBlinding: save agg chunk %d: %w", chunk, err)
		}
		line, _ := json.Marshal(nullCancLine{P: patientID, C: chunk, S: slots, V: rpNeg})
		cancF.Write(line)
		cancF.Write([]byte{'\n'})
	}
	return nil
}

// batch enrollment: each agg chunk is loaded and written once
func (b *Backend) SetupAllPatientsBlinding(providerDir, proxyDir string, patients map[string][]int) error {
	if len(patients) == 0 {
		return nil
	}
	blindDir := filepath.Join(providerDir, "blindings")
	cancDir := filepath.Join(proxyDir, "cancellations")
	if err := os.MkdirAll(blindDir, 0o755); err != nil {
		return err
	}
	if err := os.MkdirAll(cancDir, 0o755); err != nil {
		return err
	}

	chunkAgg := map[int][]uint64{}
	cancF, err := os.OpenFile(cancNDJSONPath(cancDir), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return fmt.Errorf("SetupAllPatientsBlinding: open canc store: %w", err)
	}
	defer cancF.Close()

	for patientID, rowIndices := range patients {
		rp, err := sampleBlindingValue()
		if err != nil {
			return err
		}
		rpNeg := plaintextModulus - rp
		chunkSlots := map[int][]int{}
		for _, r := range rowIndices {
			chunkSlots[r/nullSlots] = append(chunkSlots[r/nullSlots], r%nullSlots)
		}
		for chunk, slots := range chunkSlots {
			if chunkAgg[chunk] == nil {
				agg, err := loadBlindingAgg(blindingAggPath(blindDir, chunk))
				if err != nil {
					return err
				}
				chunkAgg[chunk] = agg
			}
			agg := chunkAgg[chunk]
			for _, s := range slots {
				if s >= 0 && s < len(agg) {
					agg[s] = (agg[s] + rp) % plaintextModulus
				}
			}
			line, _ := json.Marshal(nullCancLine{P: patientID, C: chunk, S: slots, V: rpNeg})
			cancF.Write(line)
			cancF.Write([]byte{'\n'})
		}
	}
	for chunk, agg := range chunkAgg {
		if err := saveBlindingAgg(blindingAggPath(blindDir, chunk), agg); err != nil {
			return fmt.Errorf("SetupAllPatientsBlinding: save agg chunk %d: %w", chunk, err)
		}
	}
	return nil
}

// add the per-chunk aggregates to the result
func (b *Backend) ApplyBlinding(resultPath, blindingsDir, outPath string) error {
	zap.L().Info("ApplyBlinding", zap.String("backend", "null"), zap.String("resultPath", resultPath), zap.String("blindingsDir", blindingsDir), zap.String("outPath", outPath))
	results, err := readCTs(resultPath)
	if err != nil {
		return fmt.Errorf("ApplyBlinding: load result: %w", err)
	}
	if len(results) == 0 {
		return fmt.Errorf("ApplyBlinding: empty result file")
	}
	for k := range results {
		agg, err := loadBlindingAgg(blindingAggPath(blindingsDir, k))
		if err != nil {
			return fmt.Errorf("ApplyBlinding: load agg chunk %d: %w", k, err)
		}
		addMod(results[k], agg)
	}
	return writeCTs(outPath, results)
}

// subtract r_p for consenting patients
func (b *Backend) ApplyCancellation(resultPath, cancDir string, consentedPatients []string, outPath string) error {
	zap.L().Info("ApplyCancellation", zap.String("backend", "null"), zap.Int("consentedCount", len(consentedPatients)), zap.String("outPath", outPath))
	if len(consentedPatients) == 0 {
		return copyFile(resultPath, outPath)
	}
	results, err := readCTs(resultPath)
	if err != nil {
		return fmt.Errorf("ApplyCancellation: load result: %w", err)
	}
	if len(results) == 0 {
		return fmt.Errorf("ApplyCancellation: empty result file")
	}
	cancStore, err := loadCancNDJSON(cancDir)
	if err != nil {
		return fmt.Errorf("ApplyCancellation: load canc store: %w", err)
	}
	for _, pid := range consentedPatients {
		for _, entry := range cancStore[pid] {
			if entry.Chunk >= len(results) {
				continue
			}
			chunk := results[entry.Chunk]
			for _, s := range entry.Slots {
				if s >= 0 && s < len(chunk) {
					chunk[s] = (chunk[s] + entry.Value) % plaintextModulus
				}
			}
		}
	}
	return writeCTs(outPath, results)
}

// helpers

// dst += src slot-wise mod t
func addMod(dst, src []uint64) {
	n := len(src)
	if len(dst) < n {
		n = len(dst)
	}
	for i := range n {
		dst[i] = (dst[i] + src[i]) % plaintextModulus
	}
}

func copyFile(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	return os.WriteFile(dst, data, 0o644)
}

func sampleBlindingValue() (uint64, error) {
	max := new(big.Int).SetUint64(plaintextModulus - 1)
	rp, err := rand.Int(rand.Reader, max)
	if err != nil {
		return 0, fmt.Errorf("sampleBlindingValue: %w", err)
	}
	rp.Add(rp, big.NewInt(1)) // shift to [1, t−1]
	return rp.Uint64(), nil
}

// plaintext comparison
func evalOp(dataVal int64, op string, condVal int64) bool {
	switch op {
	case "EQ":
		return dataVal == condVal
	case "GE":
		return dataVal >= condVal
	case "GT":
		return dataVal > condVal
	case "LE":
		return dataVal <= condVal
	case "LT":
		return dataVal < condVal
	}
	return false
}

type condition struct {
	colIdx int
	val    int64
	op     string
	logic  string // NONE | AND | OR
}

// parse "colIdx,val,OP,LOGIC[,maxVal];..."; maxVal is accepted and ignored
func parseInstructions(s string) []condition {
	parts := strings.Split(strings.TrimRight(s, ";"), ";")
	out := make([]condition, 0, len(parts))
	for _, p := range parts {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		fields := strings.Split(p, ",")
		if len(fields) < 4 {
			continue
		}
		colIdx, err1 := strconv.Atoi(fields[0])
		val, err2 := strconv.ParseInt(fields[1], 10, 64)
		if err1 != nil || err2 != nil {
			continue
		}
		out = append(out, condition{
			colIdx: colIdx,
			val:    val,
			op:     strings.ToUpper(strings.TrimSpace(fields[2])),
			logic:  strings.ToUpper(strings.TrimSpace(fields[3])),
		})
	}
	return out
}

// conditions against one chunk's columns; litCTs non-nil only on the
// encrypted-literal path
// SQL precedence: AND folds into `group`, an OR flushes it into `result`
func evalConds(dataCTs [][]uint64, conds []condition, litCTs [][]uint64) ([]uint64, error) {
	var (
		result    []uint64
		group     []uint64
		hasResult bool
	)
	for i, cond := range conds {
		if cond.colIdx < 0 || cond.colIdx >= len(dataCTs) {
			return nil, fmt.Errorf("colIdx %d out of range (have %d)", cond.colIdx, len(dataCTs))
		}
		col := dataCTs[cond.colIdx]
		curr := make([]uint64, nullSlots)
		for j := range curr {
			var dataVal uint64
			if j < len(col) {
				dataVal = col[j]
			}
			var match bool
			if litCTs != nil && cond.op == "EQ" {
				var litVal uint64
				if i < len(litCTs) && j < len(litCTs[i]) {
					litVal = litCTs[i][j]
				}
				match = dataVal == litVal
			} else {
				match = evalOp(int64(dataVal), cond.op, cond.val)
			}
			if match {
				curr[j] = 1
			}
		}
		if group == nil {
			group = curr
			continue
		}
		switch cond.logic {
		case "AND":
			for j := range group {
				group[j] &= curr[j]
			}
		case "OR":
			if hasResult {
				for j := range result {
					result[j] |= group[j]
				}
			} else {
				result = group
				hasResult = true
			}
			group = curr
		default:
			return nil, fmt.Errorf("unknown logic %q", cond.logic)
		}
	}
	if group != nil {
		if hasResult {
			for j := range result {
				result[j] |= group[j]
			}
		} else {
			result = group
		}
	}
	if result == nil {
		result = make([]uint64, nullSlots)
	}
	return result, nil
}

// rows from a JSON object array, columns alphabetical
// numbers truncate to uint64, booleans map to 0/1
func loadDataJSON(path string) ([]map[string]uint64, []string, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, nil, fmt.Errorf("loadDataJSON: %w", err)
	}
	var rawRows []map[string]any
	if err := json.Unmarshal(raw, &rawRows); err != nil {
		return nil, nil, fmt.Errorf("loadDataJSON: parse: %w", err)
	}
	if len(rawRows) == 0 {
		return nil, nil, nil
	}
	colOrder := make([]string, 0, len(rawRows[0]))
	for k := range rawRows[0] {
		colOrder = append(colOrder, k)
	}
	slices.Sort(colOrder)
	rows := make([]map[string]uint64, len(rawRows))
	for i, r := range rawRows {
		rows[i] = make(map[string]uint64, len(r))
		for k, v := range r {
			switch t := v.(type) {
			case float64:
				if t > 0 {
					rows[i][k] = uint64(t)
				}
			case bool:
				if t {
					rows[i][k] = 1
				}
			}
		}
	}
	return rows, colOrder, nil
}
