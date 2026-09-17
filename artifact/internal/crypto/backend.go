// Package crypto defines the pluggable backend interface
//
// he3db (TFHEpp/HE3DB C++), lattigo (pure-Go BGV, default), engorgio and
// patdiscover (OpenFHE C++), null (plaintext)
// Select with --backend
package crypto

// Backend is stateless; all persistent state lives in dataDir
type Backend interface {
	// --- Researcher side ---

	// sk_u -> dataDir
	GenerateResearcherKey(dataDir string) error

	// result CTs -> JSON, first rows slots
	DecryptResult(dataDir, resultPath, outputPath string, rows int) error

	// query literals under sk_u, one CT per condition (paper §4.2)
	EncryptLiterals(researcherDir string, values []int64, outPath string) error

	// --- Provider side ---

	// sk_h + eval key -> dataDir
	GenerateProviderKeys(dataDir string) error

	// data.json -> encrypted_data.bin
	EncryptData(dataDir, schemaPath string) error

	// run predicate -> result_query.bin
	EvaluatePredicate(dataDir, instructions string) error

	// as EvaluatePredicate, EQ against encrypted literals (paper §4.2)
	// range conditions still use the plaintext value field
	EvaluatePredicateEncLiterals(dataDir, literalsPath, instructions string) error

	// --- Proxy side ---

	// ksk provider->researcher
	GenerateKSK(skResearcher, skProvider, kskPath string) error

	// ksk researcher->provider (paper §4.1/§4.2)
	GenerateKSKQueryForward(skResearcherDir, skProviderDir, kskPath string) error

	// ksk_{R->L} applied to literal CTs
	ApplyKSKToLiterals(kskPath, inPath, outPath string) error

	// ksk_{L->R} applied to the provider result
	Aggregate(kskPath, inputPath, outputPath string) error

	// --- Patient / consent side ---

	// per-patient r_p: +r_p into the provider aggregate, −r_p into the proxy store
	SetupPatientBlinding(providerDir, proxyDir, patientID string, rowIndices []int) error

	// batch SetupPatientBlinding; one write per agg chunk, avoids O(N²) I/O
	SetupAllPatientsBlinding(providerDir, proxyDir string, patients map[string][]int) error

	// sum blinding CTs into the result
	ApplyBlinding(resultPath, blindingsDir, outPath string) error

	// unblind consenting rows, leave the rest garbled
	ApplyCancellation(resultPath, cancellationsDir string, consentedPatients []string, outPath string) error
}

// OffsetAggregator sums per-provider results into one ct_R (paper app:protocol step 7)
// The slot rotation folds into the key-switching key, so no rotation keys and no
// slot-range coordination between providers; backends without it return one CT each
type OffsetAggregator interface {
	// largest federation-wide row count
	MaxAggregateRows() int

	// ksk_{L->R} with the slot rotation folded in; offset 0 == GenerateKSK
	GenerateKSKAtOffset(skResearcherDir, skProviderDir, kskPath string, offset int) error

	// rotate + key-switch, same offset as the ksk
	AggregateAtOffset(kskPath, inputPath, outputPath string, offset int) error

	// sum offset-placed CTs
	SumResults(inputPaths []string, outPath string) error

	// ApplyCancellation for a provider at rowOffset: consent map is
	// federation-wide, slots are local
	ApplyCancellationAtOffset(resultPath, cancellationsDir string, consentedPatients []string, outPath string, rowOffset int) error
}
