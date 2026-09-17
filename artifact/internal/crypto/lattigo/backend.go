// Keys and ciphertexts live in the caller-supplied dataDir, serialised by the
// helpers in serialize.go
package lattigo

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"

	"go.uber.org/zap"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/bgv"

	"github.com/fecad/internal/schema"
)

const literalsCTFile = "literals.bin"

// largest domain for the HomEqSmall path: its polynomial has degree 2·maxVal and
// the parameter set evaluates exactly up to degree ~511
const eqSmallDomainMax = 255

// column-major layout of encrypted_data.bin: CT[ci*NChunks+k] = col ci, chunk k
type dataMeta struct {
	NCols   int `json:"ncols"`
	NChunks int `json:"nchunks"`
}

func dataMetaPath(dir string) string { return filepath.Join(dir, "data_meta.json") }

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

// Lattigo BGV backend
type Backend struct {
	enrollMu sync.Mutex
	enroll   map[string]*enrollSession // key: providerDir+"\x00"+proxyDir
}

func New() *Backend { return &Backend{} }

func evalKeyPath(dir string) string  { return dir + "/eval_key.bin" }
func dataCTPath(dir string) string   { return dir + "/encrypted_data.bin" }
func resultCTPath(dir string) string { return dir + "/result_query.bin" }

// sk_u -> dataDir
func (b *Backend) GenerateResearcherKey(dataDir string) error {
	zap.L().Info("GenerateResearcherKey", zap.String("backend", "lattigo"), zap.String("dataDir", dataDir))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	kgen := rlwe.NewKeyGenerator(params.Parameters)
	sk := kgen.GenSecretKeyNew()
	return WriteSK(dataDir, sk)
}

// result CTs -> JSON, first rows slots
func (b *Backend) DecryptResult(dataDir, resultPath, outputPath string, rows int) error {
	zap.L().Info("DecryptResult", zap.String("backend", "lattigo"), zap.String("resultPath", resultPath), zap.String("outputPath", outputPath), zap.Int("rows", rows))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	sk, err := ReadSK(params, dataDir)
	if err != nil {
		return err
	}
	cts, err := ReadCTs(params, resultPath)
	if err != nil {
		return err
	}

	dec := rlwe.NewDecryptor(params.Parameters, sk)
	encoder := bgv.NewEncoder(params)

	var results [][]uint64
	remaining := rows
	for _, ct := range cts {
		if remaining <= 0 {
			break
		}
		pt := bgv.NewPlaintext(params, ct.Level())
		dec.Decrypt(ct, pt)
		vals := make([]uint64, params.MaxSlots())
		if err := encoder.Decode(pt, vals); err != nil {
			return fmt.Errorf("decode: %w", err)
		}
		take := params.MaxSlots()
		if take > remaining {
			take = remaining
		}
		results = append(results, vals[:take])
		remaining -= take
	}

	out, _ := json.MarshalIndent(results, "", "  ")
	return os.WriteFile(outputPath, out, 0o644)
}

// sk_h + pk_h + relin key -> dataDir
func (b *Backend) GenerateProviderKeys(dataDir string) error {
	zap.L().Info("GenerateProviderKeys", zap.String("backend", "lattigo"), zap.String("dataDir", dataDir))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	kgen := rlwe.NewKeyGenerator(params.Parameters)
	sk := kgen.GenSecretKeyNew()
	if err := WriteSK(dataDir, sk); err != nil {
		return err
	}
	// pk_h travels with the eval key: an evaluator without sk_h still has to
	// encrypt the circuit's public constants (cf. newConstEncryptor)
	if err := WritePK(dataDir, kgen.GenPublicKeyNew(sk)); err != nil {
		return err
	}
	rlk := kgen.GenRelinearizationKeyNew(sk)
	// RelinearizationKey embeds EvaluationKey; store only the embedded key
	return WriteEvalKey(evalKeyPath(dataDir), &rlk.EvaluationKey)
}

// data.json -> encrypted_data.bin + data_meta.json
// one slot per row, ceil(nRows/maxSlots) chunks per column, column-major
func (b *Backend) EncryptData(dataDir, schemaPath string) error {
	zap.L().Info("EncryptData", zap.String("backend", "lattigo"), zap.String("dataDir", dataDir))
	params, err := DefaultParams()
	if err != nil {
		return err
	}

	rows, colOrder, err := loadDataJSON(dataDir + "/data.json")
	if err != nil {
		return err
	}
	// Instructions carry schema indices, so columns must be encrypted in schema
	// order; without a schema the loader's alphabetical order is used
	if schemaPath != "" {
		sc, scErr := schema.Load(schemaPath)
		if scErr != nil {
			return fmt.Errorf("encryptData: load schema: %w", scErr)
		}
		colOrder = sc.ColumnOrder()
	}
	if len(rows) == 0 {
		return fmt.Errorf("encryptData: empty dataset")
	}

	sk, err := ReadSK(params, dataDir)
	if err != nil {
		return fmt.Errorf("encryptData: load SK: %w", err)
	}
	enc := rlwe.NewEncryptor(params.Parameters, sk)
	encoder := bgv.NewEncoder(params)

	maxSlots := params.MaxSlots()
	nCols := len(colOrder)
	nChunks := (len(rows) + maxSlots - 1) / maxSlots

	cts := make([]*rlwe.Ciphertext, nCols*nChunks)
	for ci, colName := range colOrder {
		for k := range nChunks {
			start := k * maxSlots
			end := min(start+maxSlots, len(rows))
			slots := make([]uint64, maxSlots)
			for ri := start; ri < end; ri++ {
				slots[ri-start] = rows[ri][colName]
			}
			pt := bgv.NewPlaintext(params, params.MaxLevel())
			if err := encoder.Encode(slots, pt); err != nil {
				return fmt.Errorf("encode col %s chunk %d: %w", colName, k, err)
			}
			ct, err := enc.EncryptNew(pt)
			if err != nil {
				return fmt.Errorf("encrypt col %s chunk %d: %w", colName, k, err)
			}
			cts[ci*nChunks+k] = ct
		}
	}

	if err := WriteCTs(dataCTPath(dataDir), cts); err != nil {
		return err
	}
	return writeDataMeta(dataDir, dataMeta{NCols: nCols, NChunks: nChunks})
}

// conditions against one chunk's column CTs
// litCTs non-nil only on the encrypted-literal path; else nil
func evalChunk(
	chunkCTs []*rlwe.Ciphertext,
	conds []condition,
	litCTs []*rlwe.Ciphertext,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	// SQL precedence: AND binds tighter than OR, so AND folds into `group` and
	// an OR flushes `group` into `result` and starts a new group
	var (
		group  *rlwe.Ciphertext
		result *rlwe.Ciphertext
		err    error
	)
	for i, cond := range conds {
		if cond.colIdx < 0 || cond.colIdx >= len(chunkCTs) {
			return nil, fmt.Errorf("colIdx %d out of range (have %d)", cond.colIdx, len(chunkCTs))
		}
		var res *rlwe.Ciphertext
		if litCTs != nil && cond.op == "EQ" && cond.maxVal > 0 && cond.maxVal <= eqSmallDomainMax {
			// bounded domain: degree-2·maxVal in (x−v), far shallower than
			// HomIsZero's 16 squarings
			res, err = HomEqSmall(chunkCTs[cond.colIdx], litCTs[i], cond.maxVal, params, eval, enc, encoder)
			if err != nil {
				return nil, fmt.Errorf("cond %d EQ HomEqSmall: %w", i, err)
			}
		} else if litCTs != nil && cond.op == "EQ" {
			diff := bgv.NewCiphertext(params, 1, chunkCTs[cond.colIdx].Level())
			if err = eval.Sub(chunkCTs[cond.colIdx], litCTs[i], diff); err != nil {
				return nil, fmt.Errorf("cond %d EQ sub: %w", i, err)
			}
			res, err = HomIsZero(diff, params, eval, enc, encoder)
			if err != nil {
				return nil, fmt.Errorf("cond %d EQ HomIsZero: %w", i, err)
			}
		} else {
			res, err = HomCompare(chunkCTs[cond.colIdx], cond.val, cond.maxVal, cond.op, params, eval, enc, encoder)
			if err != nil {
				return nil, fmt.Errorf("cond %d %s HomCompare: %w", i, cond.op, err)
			}
		}
		if group == nil {
			group = res
			continue
		}
		switch cond.logic {
		case "AND":
			alignLevels(eval, group, res)
			group, err = HomAND(group, res, eval)
			if err != nil {
				return nil, fmt.Errorf("cond %d combine (AND): %w", i, err)
			}
		case "OR":
			if result == nil {
				result = group
			} else {
				alignLevels(eval, result, group)
				result, err = HomOR(result, group, params, eval, enc, encoder)
				if err != nil {
					return nil, fmt.Errorf("cond %d flush group (OR): %w", i, err)
				}
			}
			group = res
		default:
			return nil, fmt.Errorf("cond %d unknown logic %q", i, cond.logic)
		}
	}
	if group != nil {
		if result == nil {
			result = group
		} else {
			alignLevels(eval, result, group)
			result, err = HomOR(result, group, params, eval, enc, encoder)
			if err != nil {
				return nil, fmt.Errorf("final flush (OR): %w", err)
			}
		}
	}
	return result, nil
}

// encryptor for the circuit's public constants (thresholds, Lagrange coeffs)
// Co-located data holder/evaluator has sk_h and uses the cheaper secret-key
// encryption; a storage-disjoint evaluator (paper §7.2) only has pk_h
func newConstEncryptor(params bgv.Parameters, dataDir string) (*rlwe.Encryptor, error) {
	if sk, err := ReadSK(params, dataDir); err == nil {
		return rlwe.NewEncryptor(params.Parameters, sk), nil
	}
	pk, err := ReadPK(params, dataDir)
	if err != nil {
		return nil, fmt.Errorf("no sk_h and no pk_h in %s: %w", dataDir, err)
	}
	return rlwe.NewEncryptor(params.Parameters, pk), nil
}

// predicate -> result_query.bin, one CT per chunk
func (b *Backend) EvaluatePredicate(dataDir, instructions string) error {
	zap.L().Info("EvaluatePredicate", zap.String("backend", "lattigo"), zap.String("dataDir", dataDir), zap.String("instructions", instructions))
	params, err := DefaultParams()
	if err != nil {
		return err
	}

	rawEvk, err := ReadEvalKey(params, evalKeyPath(dataDir))
	if err != nil {
		return fmt.Errorf("load eval key: %w", err)
	}
	rlk := &rlwe.RelinearizationKey{EvaluationKey: *rawEvk}
	evkSet := rlwe.NewMemEvaluationKeySet(rlk)

	meta, err := readDataMeta(dataDir)
	if err != nil {
		return fmt.Errorf("load data meta: %w", err)
	}

	dataCTs, err := ReadCTs(params, dataCTPath(dataDir))
	if err != nil {
		return err
	}
	if len(dataCTs) != meta.NCols*meta.NChunks {
		return fmt.Errorf("EvaluatePredicate: expected %d CTs, got %d", meta.NCols*meta.NChunks, len(dataCTs))
	}

	enc, err := newConstEncryptor(params, dataDir)
	if err != nil {
		return err
	}
	encoder := bgv.NewEncoder(params)
	eval := bgv.NewEvaluator(params, evkSet)

	fallbackMaxVal := int64(params.PlaintextModulus() - 1)
	conds := parseInstructions(instructions, fallbackMaxVal)
	if len(conds) == 0 {
		return fmt.Errorf("evaluatePredicate: empty instruction string")
	}

	resultCTs := make([]*rlwe.Ciphertext, meta.NChunks)
	for k := 0; k < meta.NChunks; k++ {
		chunkCTs := make([]*rlwe.Ciphertext, meta.NCols)
		for ci := 0; ci < meta.NCols; ci++ {
			chunkCTs[ci] = dataCTs[ci*meta.NChunks+k]
		}
		res, err := evalChunk(chunkCTs, conds, nil, params, eval, enc, encoder)
		if err != nil {
			return fmt.Errorf("EvaluatePredicate chunk %d: %w", k, err)
		}
		resultCTs[k] = res
	}

	return WriteCTs(resultCTPath(dataDir), resultCTs)
}

// ksk provider->researcher; both args are dirs holding secret_key.bin
func (b *Backend) GenerateKSK(skResearcherDir, skProviderDir, kskPath string) error {
	zap.L().Info("GenerateKSK", zap.String("backend", "lattigo"), zap.String("kskPath", kskPath))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	skR, err := ReadSK(params, skResearcherDir)
	if err != nil {
		return fmt.Errorf("load researcher SK: %w", err)
	}
	skP, err := ReadSK(params, skProviderDir)
	if err != nil {
		return fmt.Errorf("load provider SK: %w", err)
	}
	kgen := rlwe.NewKeyGenerator(params.Parameters)
	// in=skP (provider encrypts data), out=skR (researcher decrypts)
	ksk := kgen.GenEvaluationKeyNew(skP, skR)
	return WriteEvalKey(kskPath, ksk)
}

// ksk_{L->R} applied to the provider result
func (b *Backend) Aggregate(kskPath, inputPath, outputPath string) error {
	zap.L().Info("Aggregate", zap.String("backend", "lattigo"), zap.String("kskPath", kskPath), zap.String("inputPath", inputPath), zap.String("outputPath", outputPath))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	ksk, err := ReadEvalKey(params, kskPath)
	if err != nil {
		return fmt.Errorf("load KSK: %w", err)
	}
	cts, err := ReadCTs(params, inputPath)
	if err != nil {
		return fmt.Errorf("load input CTs: %w", err)
	}

	// nil key set is fine — only ApplyEvaluationKey is called
	eval := bgv.NewEvaluator(params, nil)

	out := make([]*rlwe.Ciphertext, len(cts))
	for i, ct := range cts {
		dst := bgv.NewCiphertext(params, 1, ct.Level())
		if err := eval.ApplyEvaluationKey(ct, ksk, dst); err != nil {
			return fmt.Errorf("key switch ct %d: %w", i, err)
		}
		out[i] = dst
	}
	return WriteCTs(outputPath, out)
}

type condition struct {
	colIdx int
	val    int64
	op     string
	logic  string // NONE | AND | OR
	maxVal int64  // per-column domain upper bound from schema bits
}

// parse "colIdx,val,OP,LOGIC[,maxVal];..."
// LOGIC is NONE|AND|OR; maxVal is optional, else fallbackMaxVal
func parseInstructions(s string, fallbackMaxVal int64) []condition {
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
		maxVal := fallbackMaxVal
		if len(fields) >= 5 {
			if mv, err := strconv.ParseInt(strings.TrimSpace(fields[4]), 10, 64); err == nil && mv > 0 {
				maxVal = mv
			}
		}
		out = append(out, condition{
			colIdx: colIdx,
			val:    val,
			op:     strings.ToUpper(strings.TrimSpace(fields[2])),
			logic:  strings.ToUpper(strings.TrimSpace(fields[3])),
			maxVal: maxVal,
		})
	}
	return out
}

// literals under sk_u, all slots equal, one CT per condition (paper §4.2)
func (b *Backend) EncryptLiterals(researcherDir string, values []int64, outPath string) error {
	zap.L().Info("EncryptLiterals", zap.String("backend", "lattigo"), zap.Int("count", len(values)), zap.String("outPath", outPath))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	sk, err := ReadSK(params, researcherDir)
	if err != nil {
		return fmt.Errorf("EncryptLiterals: load SK: %w", err)
	}
	enc := rlwe.NewEncryptor(params.Parameters, sk)
	encoder := bgv.NewEncoder(params)

	cts := make([]*rlwe.Ciphertext, len(values))
	for i, v := range values {
		val := uint64(v)
		if v < 0 {
			val = uint64(params.PlaintextModulus()) - uint64(-v)
		}
		slots := make([]uint64, params.MaxSlots())
		for j := range slots {
			slots[j] = val
		}
		pt := bgv.NewPlaintext(params, params.MaxLevel())
		if err := encoder.Encode(slots, pt); err != nil {
			return fmt.Errorf("EncryptLiterals[%d]: encode: %w", i, err)
		}
		ct, err := enc.EncryptNew(pt)
		if err != nil {
			return fmt.Errorf("EncryptLiterals[%d]: encrypt: %w", i, err)
		}
		cts[i] = ct
	}
	return WriteCTs(outPath, cts)
}

// ksk researcher->provider, for forwarding query literals (paper §4.1/§4.2)
func (b *Backend) GenerateKSKQueryForward(skResearcherDir, skProviderDir, kskPath string) error {
	zap.L().Info("GenerateKSKQueryForward", zap.String("backend", "lattigo"), zap.String("kskPath", kskPath))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	skR, err := ReadSK(params, skResearcherDir)
	if err != nil {
		return fmt.Errorf("load researcher SK: %w", err)
	}
	skP, err := ReadSK(params, skProviderDir)
	if err != nil {
		return fmt.Errorf("load provider SK: %w", err)
	}
	kgen := rlwe.NewKeyGenerator(params.Parameters)
	// in=skR (researcher encrypts literals), out=skP (provider evaluates)
	ksk := kgen.GenEvaluationKeyNew(skR, skP)
	return WriteEvalKey(kskPath, ksk)
}

// ksk_{R->L} applied to literal CTs
func (b *Backend) ApplyKSKToLiterals(kskPath, inPath, outPath string) error {
	zap.L().Info("ApplyKSKToLiterals", zap.String("backend", "lattigo"), zap.String("kskPath", kskPath), zap.String("inPath", inPath), zap.String("outPath", outPath))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	ksk, err := ReadEvalKey(params, kskPath)
	if err != nil {
		return fmt.Errorf("ApplyKSKToLiterals: load KSK: %w", err)
	}
	cts, err := ReadCTs(params, inPath)
	if err != nil {
		return fmt.Errorf("ApplyKSKToLiterals: load CTs: %w", err)
	}
	eval := bgv.NewEvaluator(params, nil)
	out := make([]*rlwe.Ciphertext, len(cts))
	for i, ct := range cts {
		dst := bgv.NewCiphertext(params, 1, ct.Level())
		if err := eval.ApplyEvaluationKey(ct, ksk, dst); err != nil {
			return fmt.Errorf("ApplyKSKToLiterals: ct %d: %w", i, err)
		}
		out[i] = dst
	}
	return WriteCTs(outPath, out)
}

// predicate with pre-key-switched literal CTs for EQ (paper §4.2)
// range conditions still use the plaintext value field
func (b *Backend) EvaluatePredicateEncLiterals(dataDir, literalsPath, instructions string) error {
	zap.L().Info("EvaluatePredicateEncLiterals", zap.String("backend", "lattigo"), zap.String("dataDir", dataDir), zap.String("literalsPath", literalsPath), zap.String("instructions", instructions))
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	rawEvk, err := ReadEvalKey(params, evalKeyPath(dataDir))
	if err != nil {
		return fmt.Errorf("load eval key: %w", err)
	}
	rlk := &rlwe.RelinearizationKey{EvaluationKey: *rawEvk}
	evkSet := rlwe.NewMemEvaluationKeySet(rlk)

	meta, err := readDataMeta(dataDir)
	if err != nil {
		return fmt.Errorf("load data meta: %w", err)
	}

	dataCTs, err := ReadCTs(params, dataCTPath(dataDir))
	if err != nil {
		return fmt.Errorf("load data CTs: %w", err)
	}
	if len(dataCTs) != meta.NCols*meta.NChunks {
		return fmt.Errorf("EvaluatePredicateEncLiterals: expected %d CTs, got %d", meta.NCols*meta.NChunks, len(dataCTs))
	}

	litCTs, err := ReadCTs(params, literalsPath)
	if err != nil {
		return fmt.Errorf("load literal CTs: %w", err)
	}

	enc, err := newConstEncryptor(params, dataDir)
	if err != nil {
		return err
	}
	encoder := bgv.NewEncoder(params)
	eval := bgv.NewEvaluator(params, evkSet)

	fallbackMaxVal := int64(params.PlaintextModulus() - 1)
	conds := parseInstructions(instructions, fallbackMaxVal)
	if len(conds) == 0 {
		return fmt.Errorf("EvaluatePredicateEncLiterals: empty instructions")
	}
	if len(litCTs) < len(conds) {
		return fmt.Errorf("EvaluatePredicateEncLiterals: need %d literal CTs, have %d",
			len(conds), len(litCTs))
	}

	resultCTs := make([]*rlwe.Ciphertext, meta.NChunks)
	for k := 0; k < meta.NChunks; k++ {
		chunkCTs := make([]*rlwe.Ciphertext, meta.NCols)
		for ci := 0; ci < meta.NCols; ci++ {
			chunkCTs[ci] = dataCTs[ci*meta.NChunks+k]
		}
		res, err := evalChunk(chunkCTs, conds, litCTs, params, eval, enc, encoder)
		if err != nil {
			return fmt.Errorf("EvaluatePredicateEncLiterals chunk %d: %w", k, err)
		}
		resultCTs[k] = res
	}
	return WriteCTs(resultCTPath(dataDir), resultCTs)
}

// rows from a JSON object array, columns alphabetical
// numbers truncate to uint64, negatives become 0, booleans map to 0/1
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
