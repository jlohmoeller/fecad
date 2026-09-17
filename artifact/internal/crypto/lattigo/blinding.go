package lattigo

// Consent-blinding helpers (paper §3.3)

import (
	"bufio"
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/big"
	"os"
	"path/filepath"
	"sync"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/bgv"
)

const (
	blindingSubdir     = "blindings"
	cancellationSubdir = "cancellations"

	// Level of the masks, and so of Filter_C. Level 0 leaves no budget for
	// Filter_C's plaintext multiplication — the 0/1 mask encodes to a dense
	// polynomial with coefficients up to t — and every result came back garbled
	maskLevel = 1
)

// blinding log for chunk k; blindingsDir is the full subdirectory path
func blindChunkPath(blindingsDir string, chunk int) string {
	return filepath.Join(blindingsDir, fmt.Sprintf("chunk_%d.bin", chunk))
}

// compact cancellation store; cancDir is the full subdirectory path
func cancIndexPath(cancDir string) string { return filepath.Join(cancDir, "store.idx") }
func cancDataPath(cancDir string) string  { return filepath.Join(cancDir, "store.dat") }

// Per-(providerDir, proxyDir) enrollment state: loaded SK and open append
// handles, so only the first patient pays for the file opens
// Enrollment is sequential, so only the session map needs Backend.enrollMu
type enrollSession struct {
	sk       *rlwe.SecretKey
	params   bgv.Parameters
	blindDir string
	// Running +r_p aggregate per chunk, flushed as one CT per chunk; a CT per
	// (patient, chunk) would be ~5 GB of scratch at 1e4 patients
	blindAcc map[int]*rlwe.Ciphertext
	// Cached across calls: a fresh evaluator per call dominated enrollment time
	evalAdd *bgv.Evaluator
	// Cached too, but rotated every encryptorRotate masks: the encryptor's PRNG
	// is a blake2b XOF that Lattigo caps at 4 GiB and then panics with EOF
	// instead of reseeding, and one mask draws a whole uniform polynomial
	enc        *rlwe.Encryptor
	encoder    *bgv.Encoder
	encCount   int
	cancDat    *os.File
	cancIdx    *bufio.Writer // line-buffered writer to store.idx
	cancIdxF   *os.File      // underlying file (kept for Close)
	cancOffset int64         // current write position in store.dat
}

// Masks per encryptor: each draws ~1 MB of randomness against a 4 GiB XOF
// budget, so this stays an order of magnitude below exhaustion
const encryptorRotate = 1024

// session encryptor, replaced every encryptorRotate masks
func (s *enrollSession) maskEncryptor(params bgv.Parameters) *rlwe.Encryptor {
	if s.encCount >= encryptorRotate && s.sk != nil {
		s.enc = rlwe.NewEncryptor(params.Parameters, s.sk)
		s.encCount = 0
	}
	s.encCount += 2 // one blinding mask and one cancellation mask per call
	return s.enc
}

func (s *enrollSession) closeFiles() {
	if s.cancIdx != nil {
		s.cancIdx.Flush()
	}
	if s.cancIdxF != nil {
		s.cancIdxF.Close()
	}
	if s.cancDat != nil {
		s.cancDat.Close()
	}
}

// per-chunk aggregate -> one CT per chunk file, overwritten each flush
func (s *enrollSession) flushBlindAggregate() error {
	if s.blindAcc == nil {
		return nil
	}
	for chunk, ct := range s.blindAcc {
		if ct == nil {
			continue
		}
		bytes, err := ct.MarshalBinary()
		if err != nil {
			return fmt.Errorf("flushBlindAggregate chunk %d marshal: %w", chunk, err)
		}
		// Temp file + rename: truncating in place raced the evaluator's read,
		// which then blinded nothing and lost that provider's rows to garbage
		// after cancellation
		path := blindChunkPath(s.blindDir, chunk)
		tmp := path + ".tmp"
		f, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
		if err != nil {
			return fmt.Errorf("flushBlindAggregate chunk %d open: %w", chunk, err)
		}
		var hdr [4]byte
		binary.BigEndian.PutUint32(hdr[:], uint32(len(bytes)))
		if _, err := f.Write(hdr[:]); err != nil {
			f.Close()
			return fmt.Errorf("flushBlindAggregate chunk %d header: %w", chunk, err)
		}
		if _, err := f.Write(bytes); err != nil {
			f.Close()
			return fmt.Errorf("flushBlindAggregate chunk %d body: %w", chunk, err)
		}
		if err := f.Sync(); err != nil {
			f.Close()
			return fmt.Errorf("flushBlindAggregate chunk %d sync: %w", chunk, err)
		}
		if err := f.Close(); err != nil {
			return fmt.Errorf("flushBlindAggregate chunk %d close: %w", chunk, err)
		}
		if err := os.Rename(tmp, path); err != nil {
			return fmt.Errorf("flushBlindAggregate chunk %d rename: %w", chunk, err)
		}
	}
	return nil
}

// Backend session cache

func (b *Backend) getOrCreateSession(providerDir, proxyDir string) (*enrollSession, error) {
	key := providerDir + "\x00" + proxyDir
	b.enrollMu.Lock()
	defer b.enrollMu.Unlock()
	if b.enroll == nil {
		b.enroll = make(map[string]*enrollSession)
	}
	if s, ok := b.enroll[key]; ok {
		return s, nil
	}

	params, err := DefaultParams()
	if err != nil {
		return nil, err
	}
	sk, err := ReadSK(params, providerDir)
	if err != nil {
		return nil, fmt.Errorf("getOrCreateSession: load SK: %w", err)
	}

	blindDir := filepath.Join(providerDir, blindingSubdir)
	cancDir := filepath.Join(proxyDir, cancellationSubdir)
	if err := os.MkdirAll(blindDir, 0o755); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(cancDir, 0o755); err != nil {
		return nil, err
	}

	cancDatF, err := os.OpenFile(cancDataPath(cancDir), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return nil, fmt.Errorf("getOrCreateSession: open store.dat: %w", err)
	}
	offset, err := cancDatF.Seek(0, io.SeekEnd)
	if err != nil {
		cancDatF.Close()
		return nil, fmt.Errorf("getOrCreateSession: seek store.dat: %w", err)
	}

	cancIdxF, err := os.OpenFile(cancIndexPath(cancDir), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		cancDatF.Close()
		return nil, fmt.Errorf("getOrCreateSession: open store.idx: %w", err)
	}

	s := &enrollSession{
		sk:         sk,
		params:     params,
		blindDir:   blindDir,
		blindAcc:   make(map[int]*rlwe.Ciphertext),
		evalAdd:    bgv.NewEvaluator(params, nil),
		enc:        rlwe.NewEncryptor(params.Parameters, sk),
		encoder:    bgv.NewEncoder(params),
		cancDat:    cancDatF,
		cancIdx:    bufio.NewWriterSize(cancIdxF, 64*1024),
		cancIdxF:   cancIdxF,
		cancOffset: offset,
	}
	b.enroll[key] = s
	return s, nil
}

// cancellation index

// one patient CT in store.dat
type cancEntry struct {
	Chunk  int   `json:"c"`
	Offset int64 `json:"o"`
	Size   int   `json:"s"`
}

// one line of store.idx
type cancIdxEntry struct {
	P string `json:"p"`
	C int    `json:"c"`
	O int64  `json:"o"`
	S int    `json:"s"`
}

// store.idx -> patient→entries
func loadCancIndex(cancDir string) (map[string][]cancEntry, error) {
	f, err := os.Open(cancIndexPath(cancDir))
	if errors.Is(err, os.ErrNotExist) {
		return map[string][]cancEntry{}, nil
	}
	if err != nil {
		return nil, err
	}
	defer f.Close()

	idx := make(map[string][]cancEntry)
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 256*1024), 256*1024)
	for sc.Scan() {
		line := sc.Bytes()
		if len(line) == 0 {
			continue
		}
		var e cancIdxEntry
		if err := json.Unmarshal(line, &e); err != nil {
			return nil, fmt.Errorf("loadCancIndex: parse line: %w", err)
		}
		idx[e.P] = append(idx[e.P], cancEntry{Chunk: e.C, Offset: e.O, Size: e.S})
	}
	return idx, sc.Err()
}

// sampleBlindingValue

func sampleBlindingValue(t uint64) (uint64, error) {
	max := new(big.Int).SetUint64(t - 1)
	rp, err := rand.Int(rand.Reader, max)
	if err != nil {
		return 0, fmt.Errorf("sampleBlindingValue: %w", err)
	}
	rp.Add(rp, big.NewInt(1))
	return rp.Uint64(), nil
}

// buildMaskCT

// maskLevel CT with slot[i] = val for i ∈ indices, else 0
func buildMaskCT(
	indices []int,
	val uint64,
	params bgv.Parameters,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	maxSlots := params.MaxSlots()
	slots := make([]uint64, maxSlots)
	for _, idx := range indices {
		if idx < 0 || idx >= maxSlots {
			return nil, fmt.Errorf("buildMaskCT: slot index %d out of range [0,%d)", idx, maxSlots)
		}
		slots[idx] = val
	}
	pt := bgv.NewPlaintext(params, maskLevel)
	if err := encoder.Encode(slots, pt); err != nil {
		return nil, fmt.Errorf("buildMaskCT encode: %w", err)
	}
	ct, err := enc.EncryptNew(pt)
	if err != nil {
		return nil, fmt.Errorf("buildMaskCT encrypt: %w", err)
	}
	return ct, nil
}

// SetupPatientBlinding

// r_p ∈ [1, t−1]: +r_p into the provider's per-chunk aggregate, −r_p into the
// proxy's cancellation store
func (b *Backend) SetupPatientBlinding(providerDir, proxyDir, patientID string, rowIndices []int) error {
	if len(rowIndices) == 0 {
		return nil
	}

	s, err := b.getOrCreateSession(providerDir, proxyDir)
	if err != nil {
		return fmt.Errorf("SetupPatientBlinding: %w", err)
	}

	params := s.params
	t := params.PlaintextModulus()
	rp, err := sampleBlindingValue(t)
	if err != nil {
		return err
	}
	rpNeg := t - rp

	maxSlots := params.MaxSlots()

	chunkSlots := map[int][]int{}
	for _, r := range rowIndices {
		chunkSlots[r/maxSlots] = append(chunkSlots[r/maxSlots], r%maxSlots)
	}

	for chunk, slots := range chunkSlots {
		// Serial with the session-cached encryptor/encoder: the parallel
		// version rebuilt Lattigo state per call and dominated enrollment
		var (
			blindCT, cancelCT   *rlwe.Ciphertext
			blindErr, cancelErr error
		)
		enc := s.maskEncryptor(params)
		blindCT, blindErr = buildMaskCT(slots, rp, params, enc, s.encoder)
		cancelCT, cancelErr = buildMaskCT(slots, rpNeg, params, enc, s.encoder)
		if blindErr != nil {
			return fmt.Errorf("SetupPatientBlinding chunk %d blinding: %w", chunk, blindErr)
		}
		if cancelErr != nil {
			return fmt.Errorf("SetupPatientBlinding chunk %d cancellation: %w", chunk, cancelErr)
		}

		// Fold into the per-chunk aggregate, flushed below, so chunk_*.bin
		// always holds the current cohort in ~6 MB rather than growing with it
		if acc, ok := s.blindAcc[chunk]; ok && acc != nil {
			if err := s.evalAdd.Add(acc, blindCT, acc); err != nil {
				return fmt.Errorf("SetupPatientBlinding chunk %d accumulate blind: %w", chunk, err)
			}
		} else {
			s.blindAcc[chunk] = blindCT
		}

		ctBytes, err := cancelCT.MarshalBinary()
		if err != nil {
			return fmt.Errorf("SetupPatientBlinding chunk %d marshal cancel: %w", chunk, err)
		}
		entryOffset := s.cancOffset
		if _, err := s.cancDat.Write(ctBytes); err != nil {
			return fmt.Errorf("SetupPatientBlinding chunk %d write cancel: %w", chunk, err)
		}
		s.cancOffset += int64(len(ctBytes))

		entry, _ := json.Marshal(cancIdxEntry{P: patientID, C: chunk, O: entryOffset, S: len(ctBytes)})
		s.cancIdx.Write(entry)
		s.cancIdx.WriteByte('\n')
	}
	if err := s.flushBlindAggregate(); err != nil {
		return err
	}
	return nil
}

// batch enrollment; the session handles make each patient O(1) I/O
func (b *Backend) SetupAllPatientsBlinding(providerDir, proxyDir string, patients map[string][]int) error {
	for pid, rows := range patients {
		if err := b.SetupPatientBlinding(providerDir, proxyDir, pid, rows); err != nil {
			return err
		}
	}
	return nil
}

// Modulus-switch ct down to target, one prime per step. DropLevel is wrong
// here: truncating limbs keeps the plaintext only while the noise is tiny, so a
// fresh mask survives it but an evaluated result decrypts to noise
func rescaleTo(eval *bgv.Evaluator, ct *rlwe.Ciphertext, target int) error {
	for ct.Level() > target {
		if err := eval.Rescale(ct, ct); err != nil {
			return fmt.Errorf("rescale to level %d: %w", target, err)
		}
	}
	return nil
}

// ApplyBlinding

// sum each chunk's blinding CTs into the matching result CT
func (b *Backend) ApplyBlinding(resultPath, blindingsDir, outPath string) error {
	b.flushEnrollSessions()

	params, err := DefaultParams()
	if err != nil {
		return err
	}
	results, err := ReadCTs(params, resultPath)
	if err != nil {
		return fmt.Errorf("ApplyBlinding: load result: %w", err)
	}
	if len(results) == 0 {
		return fmt.Errorf("ApplyBlinding: empty result file")
	}

	eval := bgv.NewEvaluator(params, nil)
	for k, resCT := range results {
		blindCT, err := loadAndSumBlindChunk(params, blindingsDir, k)
		if err != nil {
			return fmt.Errorf("ApplyBlinding chunk %d: %w", k, err)
		}
		if blindCT == nil {
			continue
		}
		if blindCT.Level() < resCT.Level() {
			if err2 := rescaleTo(eval, resCT, blindCT.Level()); err2 != nil {
				return fmt.Errorf("ApplyBlinding chunk %d: %w", k, err2)
			}
		}
		if err2 := eval.Add(resCT, blindCT, resCT); err2 != nil {
			return fmt.Errorf("ApplyBlinding chunk %d add: %w", k, err2)
		}
	}
	return WriteCTs(outPath, results)
}

// sum of chunk_{k}.bin; nil when the file is absent (nobody enrolled)
func loadAndSumBlindChunk(params bgv.Parameters, blindingsDir string, chunk int) (*rlwe.Ciphertext, error) {
	f, err := os.Open(blindChunkPath(blindingsDir, chunk))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	defer f.Close()

	eval := bgv.NewEvaluator(params, nil)
	var acc *rlwe.Ciphertext
	var hdr [4]byte
	for {
		if _, err := io.ReadFull(f, hdr[:]); err == io.EOF || errors.Is(err, io.ErrUnexpectedEOF) {
			break
		} else if err != nil {
			return nil, fmt.Errorf("read chunk %d header: %w", chunk, err)
		}
		size := binary.BigEndian.Uint32(hdr[:])
		buf := make([]byte, size)
		if _, err := io.ReadFull(f, buf); err != nil {
			return nil, fmt.Errorf("read chunk %d CT: %w", chunk, err)
		}
		ct := bgv.NewCiphertext(params, 1, maskLevel)
		if err := ct.UnmarshalBinary(buf); err != nil {
			return nil, fmt.Errorf("unmarshal chunk %d CT: %w", chunk, err)
		}
		if acc == nil {
			acc = ct
		} else {
			if err := eval.Add(acc, ct, acc); err != nil {
				return nil, fmt.Errorf("add chunk %d CT: %w", chunk, err)
			}
		}
	}
	return acc, nil
}

// flush buffered writers of every active session
func (b *Backend) flushEnrollSessions() {
	b.enrollMu.Lock()
	defer b.enrollMu.Unlock()
	for _, s := range b.enroll {
		if s.cancIdx != nil {
			s.cancIdx.Flush()
		}
		if err := s.flushBlindAggregate(); err != nil {
			// the read path surfaces this; no logger here
			_ = err
		}
	}
}

// ApplyCancellation

// Add the −r_p masks of the consenting patients, then Filter_C (paper
// app:protocol step 6): non-consenting slots are zeroed by plaintext
// multiplication so the user cannot read the consent map off ct_R
func (b *Backend) ApplyCancellation(resultPath, cancellationsDir string, consentedPatients []string, outPath string) error {
	return b.applyCancellation(resultPath, cancellationsDir, consentedPatients, outPath, 0)
}

// ApplyCancellation for a provider at [rowOffset, rowOffset+n): the consent map
// is federation-wide, the slots are local, so the filter reads
// rowToPatient[rowOffset+slot]
func (b *Backend) ApplyCancellationAtOffset(resultPath, cancellationsDir string, consentedPatients []string, outPath string, rowOffset int) error {
	return b.applyCancellation(resultPath, cancellationsDir, consentedPatients, outPath, rowOffset)
}

func (b *Backend) applyCancellation(resultPath, cancellationsDir string, consentedPatients []string, outPath string, rowOffset int) error {
	b.flushEnrollSessions()

	params, err := DefaultParams()
	if err != nil {
		return err
	}
	results, err := ReadCTs(params, resultPath)
	if err != nil {
		return fmt.Errorf("ApplyCancellation: load result: %w", err)
	}
	if len(results) == 0 {
		return fmt.Errorf("ApplyCancellation: empty result file")
	}

	// Nobody consenting means no r_p to cancel, but Filter_C below still runs:
	// handing back match + r_p residues leaks match-bit deltas across queries
	if len(consentedPatients) > 0 {
		idx, idxErr := loadCancIndex(cancellationsDir)
		if idxErr == nil && len(idx) > 0 {
			if err2 := applyCancellationCompact(params, results, cancellationsDir, idx, consentedPatients); err2 != nil {
				return fmt.Errorf("ApplyCancellation compact: %w", err2)
			}
		} else {
			if err2 := applyCancellationLegacy(params, results, cancellationsDir, consentedPatients); err2 != nil {
				return fmt.Errorf("ApplyCancellation legacy: %w", err2)
			}
		}
	}

	if err := applyConsentFilter(params, results, cancellationsDir, consentedPatients, rowOffset); err != nil {
		return fmt.Errorf("ApplyCancellation filter: %w", err)
	}

	return WriteCTs(outPath, results)
}

// Zero every slot whose patient is not consenting, by plaintext multiplication
// with a 0/1 mask; no-op when row_to_patient.json is missing
func applyConsentFilter(
	params bgv.Parameters,
	results []*rlwe.Ciphertext,
	cancDir string,
	consented []string,
	rowOffset int,
) error {
	rowToPatient, err := loadRowToPatient(cancDir)
	if err != nil {
		return err
	}
	if len(rowToPatient) == 0 {
		return nil
	}

	consentSet := make(map[string]struct{}, len(consented))
	for _, pid := range consented {
		consentSet[pid] = struct{}{}
	}

	maxSlots := params.MaxSlots()
	encoder := bgv.NewEncoder(params)
	eval := bgv.NewEvaluator(params, nil)

	for chunk, ct := range results {
		base := rowOffset + chunk*maxSlots
		mask := make([]uint64, maxSlots)
		for s := 0; s < maxSlots; s++ {
			row := base + s
			if row >= len(rowToPatient) {
				break
			}
			if _, ok := consentSet[rowToPatient[row]]; ok {
				mask[s] = 1
			}
		}
		// an all-zero mask still costs a mul, which is what keeps the consent
		// map hidden
		pt := bgv.NewPlaintext(params, ct.Level())
		if err := encoder.Encode(mask, pt); err != nil {
			return fmt.Errorf("encode mask chunk %d: %w", chunk, err)
		}
		if err := eval.Mul(ct, pt, ct); err != nil {
			return fmt.Errorf("mul mask chunk %d: %w", chunk, err)
		}
	}
	return nil
}

// row_to_patient.json: entry r is the patient owning row r; empty when absent
func loadRowToPatient(cancDir string) ([]string, error) {
	path := filepath.Join(cancDir, "row_to_patient.json")
	raw, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, fmt.Errorf("loadRowToPatient: %w", err)
	}
	var rtp []string
	if err := json.Unmarshal(raw, &rtp); err != nil {
		return nil, fmt.Errorf("loadRowToPatient: parse: %w", err)
	}
	return rtp, nil
}

func applyCancellationCompact(
	params bgv.Parameters,
	results []*rlwe.Ciphertext,
	cancDir string,
	idx map[string][]cancEntry,
	consented []string,
) error {
	datPath := cancDataPath(cancDir)
	f, err := os.Open(datPath)
	if err != nil {
		return fmt.Errorf("open store.dat: %w", err)
	}
	defer f.Close()

	eval := bgv.NewEvaluator(params, nil)
	for _, pid := range consented {
		entries, ok := idx[pid]
		if !ok {
			continue
		}
		for _, entry := range entries {
			if entry.Chunk >= len(results) {
				continue
			}
			buf := make([]byte, entry.Size)
			if _, err2 := f.ReadAt(buf, entry.Offset); err2 != nil {
				return fmt.Errorf("read CT for %s chunk %d: %w", pid, entry.Chunk, err2)
			}
			ct := bgv.NewCiphertext(params, 1, maskLevel)
			if err2 := ct.UnmarshalBinary(buf); err2 != nil {
				return fmt.Errorf("unmarshal CT for %s chunk %d: %w", pid, entry.Chunk, err2)
			}
			res := results[entry.Chunk]
			if ct.Level() < res.Level() {
				if err2 := rescaleTo(eval, res, ct.Level()); err2 != nil {
					return err2
				}
			}
			if err2 := eval.Add(res, ct, res); err2 != nil {
				return fmt.Errorf("add CT for %s chunk %d: %w", pid, entry.Chunk, err2)
			}
		}
	}
	return nil
}

// old per-file approach with parallel loads
func applyCancellationLegacy(
	params bgv.Parameters,
	results []*rlwe.Ciphertext,
	cancDir string,
	consented []string,
) error {
	type result struct {
		ct    *rlwe.Ciphertext
		chunk int
		err   error
	}
	loaded := make([]result, len(consented))
	var wg sync.WaitGroup
	sem := make(chan struct{}, 32)
	for i, pid := range consented {
		wg.Add(1)
		go func(i int, pid string) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			path := filepath.Join(cancDir, pid+".bin")
			cts, err := ReadCTs(params, path)
			if err != nil {
				loaded[i] = result{err: err}
				return
			}
			if len(cts) == 0 {
				return
			}
			loaded[i] = result{ct: cts[0], chunk: 0}
		}(i, pid)
	}
	wg.Wait()

	eval := bgv.NewEvaluator(params, nil)
	for _, r := range loaded {
		if r.err != nil || r.ct == nil {
			continue
		}
		if r.chunk >= len(results) {
			continue
		}
		res := results[r.chunk]
		if r.ct.Level() < res.Level() {
			if err := rescaleTo(eval, res, r.ct.Level()); err != nil {
				return err
			}
		}
		if err := eval.Add(res, r.ct, res); err != nil {
			return fmt.Errorf("add legacy CT: %w", err)
		}
	}
	return nil
}

// helpers for tests / bench

func sumCTs(cts []*rlwe.Ciphertext, eval *bgv.Evaluator) (*rlwe.Ciphertext, error) {
	if len(cts) == 0 {
		return nil, fmt.Errorf("sumCTs: empty slice")
	}
	acc := cts[0].CopyNew()
	for _, ct := range cts[1:] {
		if err := eval.Add(acc, ct, acc); err != nil {
			return nil, fmt.Errorf("sumCTs: add: %w", err)
		}
	}
	return acc, nil
}

func loadCTsFromDir(params bgv.Parameters, dir string) ([]*rlwe.Ciphertext, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil, nil
		}
		return nil, fmt.Errorf("loadCTsFromDir: %w", err)
	}
	var cts []*rlwe.Ciphertext
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".bin" {
			continue
		}
		loaded, err := ReadCTs(params, filepath.Join(dir, e.Name()))
		if err != nil {
			return nil, fmt.Errorf("loadCTsFromDir: load %q: %w", e.Name(), err)
		}
		cts = append(cts, loaded...)
	}
	return cts, nil
}

func loadNamedCTs(params bgv.Parameters, dir string, names []string) ([]*rlwe.Ciphertext, error) {
	cts := make([]*rlwe.Ciphertext, len(names))
	errs := make([]error, len(names))
	var wg sync.WaitGroup
	sem := make(chan struct{}, 32)
	for i, name := range names {
		wg.Add(1)
		go func(i int, name string) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			loaded, err := ReadCTs(params, filepath.Join(dir, name+".bin"))
			if err != nil {
				errs[i] = err
				return
			}
			if len(loaded) > 0 {
				cts[i] = loaded[0]
			}
		}(i, name)
	}
	wg.Wait()
	for _, e := range errs {
		if e != nil {
			return nil, e
		}
	}
	var out []*rlwe.Ciphertext
	for _, ct := range cts {
		if ct != nil {
			out = append(out, ct)
		}
	}
	return out, nil
}

func addCTsToResult(
	params bgv.Parameters,
	resultPath string,
	toAdd []*rlwe.Ciphertext,
	outPath string,
) error {
	results, err := ReadCTs(params, resultPath)
	if err != nil {
		return fmt.Errorf("addCTsToResult: load result: %w", err)
	}
	if len(results) == 0 {
		return fmt.Errorf("addCTsToResult: empty result file")
	}
	eval := bgv.NewEvaluator(params, nil)
	acc := results[0]
	for _, delta := range toAdd {
		if delta.Level() < acc.Level() {
			if err := rescaleTo(eval, acc, delta.Level()); err != nil {
				return err
			}
		}
		if err := eval.Add(acc, delta, acc); err != nil {
			return fmt.Errorf("addCTsToResult: add: %w", err)
		}
	}
	return WriteCTs(outPath, []*rlwe.Ciphertext{acc})
}
