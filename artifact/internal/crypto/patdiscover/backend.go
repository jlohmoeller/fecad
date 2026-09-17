// Package patdiscover implements crypto.Backend over libpatdiscover_bridge
package patdiscover

/*
#cgo LDFLAGS: -L${SRCDIR}/../../../cpp/lib -lpatdiscover_bridge -lstdc++ -lm
#cgo linux LDFLAGS: -Wl,-rpath,${SRCDIR}/../../../cpp/lib
#cgo darwin LDFLAGS: -Wl,-rpath,@loader_path/../../../cpp/lib
#include "../../../cpp/src/patdiscover_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"encoding/json"
	"fmt"
	"os"
	"sync"
	"unsafe"

	"go.uber.org/zap"
)

// Serializes every cgo entry: OpenFHE's EvalMultKey table is a process-global
// static map that the bridge clears on every evaluate to re-prime it with the
// per-provider key, so a parallel fan-out double-frees it. Per-provider timings
// stay correct; for parallel measurement use one OS process per provider
var cgoMu sync.Mutex

type Backend struct{}

// buildDir is unused by the CGo backend
func New(_ string) *Backend { return &Backend{} }

func cstr(s string) (*C.char, func()) {
	cs := C.CString(s)
	return cs, func() { C.free(unsafe.Pointer(cs)) }
}

func check(rc C.int, errPtr *C.char) error {
	if rc == 0 {
		return nil
	}
	if errPtr != nil {
		msg := C.GoString(errPtr)
		C.patdiscover_free_string(errPtr)
		return fmt.Errorf("patdiscover: %s", msg)
	}
	return fmt.Errorf("patdiscover: call failed (rc=%d)", int(rc))
}

func (b *Backend) GenerateResearcherKey(dataDir string) error {
	zap.L().Info("GenerateResearcherKey", zap.String("backend", "patdiscover"), zap.String("dataDir", dataDir))
	dir, free := cstr(dataDir)
	defer free()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_researcher_generate_key(dir, &ep), ep)
}

func (b *Backend) DecryptResult(dataDir, resultPath, outputPath string, rows int) error {
	zap.L().Info("DecryptResult", zap.String("backend", "patdiscover"), zap.String("resultPath", resultPath), zap.String("outputPath", outputPath), zap.Int("rows", rows))
	dir, fDir := cstr(dataDir)
	defer fDir()
	rp, fRp := cstr(resultPath)
	defer fRp()
	op, fOp := cstr(outputPath)
	defer fOp()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_researcher_decrypt(dir, rp, op, C.int(rows), &ep), ep)
}

func (b *Backend) GenerateProviderKeys(dataDir string) error {
	zap.L().Info("GenerateProviderKeys", zap.String("backend", "patdiscover"), zap.String("dataDir", dataDir))
	dir, free := cstr(dataDir)
	defer free()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_provider_generate_keys(dir, &ep), ep)
}

func (b *Backend) EncryptData(dataDir, schemaPath string) error {
	zap.L().Info("EncryptData", zap.String("backend", "patdiscover"), zap.String("dataDir", dataDir))
	dir, fDir := cstr(dataDir)
	defer fDir()
	sp, fSp := cstr(schemaPath)
	defer fSp()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_provider_encrypt(dir, sp, &ep), ep)
}

func (b *Backend) EvaluatePredicate(dataDir, instructions string) error {
	zap.L().Info("EvaluatePredicate", zap.String("backend", "patdiscover"), zap.String("dataDir", dataDir), zap.String("instructions", instructions))
	dir, fDir := cstr(dataDir)
	defer fDir()
	ins, fIns := cstr(instructions)
	defer fIns()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_provider_evaluate(dir, ins, &ep), ep)
}

func (b *Backend) GenerateKSK(skResearcher, skProvider, kskPath string) error {
	zap.L().Info("GenerateKSK", zap.String("backend", "patdiscover"), zap.String("kskPath", kskPath))
	skR, fR := cstr(skResearcher)
	defer fR()
	skP, fP := cstr(skProvider)
	defer fP()
	ksk, fK := cstr(kskPath)
	defer fK()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_proxy_generate_ksk(skR, skP, ksk, &ep), ep)
}

func (b *Backend) Aggregate(kskPath, inputPath, outputPath string) error {
	zap.L().Info("Aggregate", zap.String("backend", "patdiscover"), zap.String("kskPath", kskPath), zap.String("inputPath", inputPath), zap.String("outputPath", outputPath))
	ksk, fK := cstr(kskPath)
	defer fK()
	inp, fI := cstr(inputPath)
	defer fI()
	out, fO := cstr(outputPath)
	defer fO()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_proxy_reencrypt(ksk, inp, out, &ep), ep)
}

func (b *Backend) GenerateKSKQueryForward(skResearcherDir, skProviderDir, kskPath string) error {
	zap.L().Info("GenerateKSKQueryForward", zap.String("backend", "patdiscover"), zap.String("kskPath", kskPath))
	rdir, fR := cstr(skResearcherDir)
	defer fR()
	pdir, fP := cstr(skProviderDir)
	defer fP()
	ksk, fK := cstr(kskPath)
	defer fK()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_proxy_generate_ksk_rl(rdir, pdir, ksk, &ep), ep)
}

func (b *Backend) ApplyKSKToLiterals(kskPath, inPath, outPath string) error {
	zap.L().Info("ApplyKSKToLiterals", zap.String("backend", "patdiscover"), zap.String("kskPath", kskPath), zap.String("inPath", inPath), zap.String("outPath", outPath))
	ksk, fK := cstr(kskPath)
	defer fK()
	inp, fI := cstr(inPath)
	defer fI()
	out, fO := cstr(outPath)
	defer fO()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_proxy_reencrypt_literals(ksk, inp, out, &ep), ep)
}

func (b *Backend) EncryptLiterals(researcherDir string, values []int64, outPath string) error {
	zap.L().Info("EncryptLiterals", zap.String("backend", "patdiscover"), zap.Int("count", len(values)), zap.String("outPath", outPath))
	vJSON, _ := json.Marshal(values)
	rdir, fR := cstr(researcherDir)
	defer fR()
	vj, fV := cstr(string(vJSON))
	defer fV()
	op, fO := cstr(outPath)
	defer fO()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_encrypt_literals(rdir, vj, op, &ep), ep)
}

func (b *Backend) EvaluatePredicateEncLiterals(dataDir, literalsPath, instructions string) error {
	zap.L().Info("EvaluatePredicateEncLiterals", zap.String("backend", "patdiscover"), zap.String("dataDir", dataDir), zap.String("literalsPath", literalsPath), zap.String("instructions", instructions))
	dir, fD := cstr(dataDir)
	defer fD()
	ins, fI := cstr(instructions)
	defer fI()
	lp, fL := cstr(literalsPath)
	defer fL()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_provider_evaluate_enc_literals(dir, ins, lp, &ep), ep)
}

// One CGo call per batch: every chunk log and the cancellation store are opened
// once regardless of patient count
func (b *Backend) setupBlinding(providerDir, proxyDir string, patients map[string][]int) error {
	pJSON, _ := json.Marshal(patients)
	pd, fPd := cstr(providerDir)
	defer fPd()
	qd, fQd := cstr(proxyDir)
	defer fQd()
	pj, fPj := cstr(string(pJSON))
	defer fPj()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_setup_blinding(pd, qd, pj, &ep), ep)
}

func (b *Backend) SetupPatientBlinding(providerDir, proxyDir, patientID string, rowIndices []int) error {
	return b.setupBlinding(providerDir, proxyDir, map[string][]int{patientID: rowIndices})
}

func (b *Backend) SetupAllPatientsBlinding(providerDir, proxyDir string, patients map[string][]int) error {
	return b.setupBlinding(providerDir, proxyDir, patients)
}

func (b *Backend) ApplyBlinding(resultPath, blindingsDir, outPath string) error {
	zap.L().Info("ApplyBlinding", zap.String("backend", "patdiscover"), zap.String("resultPath", resultPath), zap.String("outPath", outPath))
	bd, fBd := cstr(blindingsDir)
	defer fBd()
	rp, fRp := cstr(resultPath)
	defer fRp()
	op, fOp := cstr(outPath)
	defer fOp()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_apply_blinding(bd, rp, op, &ep), ep)
}

func (b *Backend) ApplyCancellation(resultPath, cancellationsDir string, consentedPatients []string, outPath string) error {
	zap.L().Info("ApplyCancellation", zap.String("backend", "patdiscover"), zap.Int("consentedCount", len(consentedPatients)), zap.String("outPath", outPath))
	if len(consentedPatients) == 0 {
		data, err := os.ReadFile(resultPath)
		if err != nil {
			return err
		}
		return os.WriteFile(outPath, data, 0o644)
	}
	cJSON, _ := json.Marshal(consentedPatients)
	rp, fRp := cstr(resultPath)
	defer fRp()
	cd, fCd := cstr(cancellationsDir)
	defer fCd()
	cj, fCj := cstr(string(cJSON))
	defer fCj()
	op, fOp := cstr(outPath)
	defer fOp()
	cgoMu.Lock()
	defer cgoMu.Unlock()
	var ep *C.char
	return check(C.patdiscover_apply_cancellation(rp, cd, cj, op, &ep), ep)
}
