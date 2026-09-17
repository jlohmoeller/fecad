package lattigo

// Cross-provider aggregation (paper app:protocol step 7)

import (
	"fmt"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/ring"
	"github.com/tuneinsight/lattigo/v6/schemes/bgv"
)

// largest total row count: one slot row, N/2
func (b *Backend) MaxAggregateRows() int {
	params, err := DefaultParams()
	if err != nil {
		return 0
	}
	return params.MaxSlots() / 2
}

// automorphism index shifting slot 0 to slot offset
func rotationIndex(params bgv.Parameters, offset int) ([]uint64, error) {
	galEl := params.GaloisElement(-offset)
	ringQ := params.RingQ()
	return ring.AutomorphismNTTIndex(ringQ.N(), ringQ.NthRoot(), galEl)
}

// ksk mapping φ_offset(sk_provider) -> sk_researcher; offset 0 == GenerateKSK
func (b *Backend) GenerateKSKAtOffset(skResearcherDir, skProviderDir, kskPath string, offset int) error {
	if offset == 0 {
		return b.GenerateKSK(skResearcherDir, skProviderDir, kskPath)
	}
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
	idx, err := rotationIndex(params, offset)
	if err != nil {
		return fmt.Errorf("automorphism index for offset %d: %w", offset, err)
	}

	ringQP := params.RingQP()
	skRot := rlwe.NewSecretKey(params)
	ringQP.RingQ.AutomorphismNTTWithIndex(skP.Value.Q, idx, skRot.Value.Q)
	if ringQP.RingP != nil {
		ringQP.RingP.AutomorphismNTTWithIndex(skP.Value.P, idx, skRot.Value.P)
	}

	kgen := rlwe.NewKeyGenerator(params.Parameters)
	return WriteEvalKey(kskPath, kgen.GenEvaluationKeyNew(skRot, skR))
}

// rotate then key-switch; the ksk must come from GenerateKSKAtOffset with the
// same offset
func (b *Backend) AggregateAtOffset(kskPath, inputPath, outputPath string, offset int) error {
	if offset == 0 {
		return b.Aggregate(kskPath, inputPath, outputPath)
	}
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
	if len(cts) != 1 {
		return fmt.Errorf("AggregateAtOffset: %d chunks, offset aggregation needs exactly 1", len(cts))
	}
	idx, err := rotationIndex(params, offset)
	if err != nil {
		return fmt.Errorf("automorphism index for offset %d: %w", offset, err)
	}

	eval := bgv.NewEvaluator(params, nil)
	ct := cts[0]
	ringQ := params.RingQ().AtLevel(ct.Level())
	rot := bgv.NewCiphertext(params, 1, ct.Level())
	*rot.MetaData = *ct.MetaData
	ringQ.AutomorphismNTTWithIndex(ct.Value[0], idx, rot.Value[0])
	ringQ.AutomorphismNTTWithIndex(ct.Value[1], idx, rot.Value[1])

	out := bgv.NewCiphertext(params, 1, rot.Level())
	if err := eval.ApplyEvaluationKey(rot, ksk, out); err != nil {
		return fmt.Errorf("key switch at offset %d: %w", offset, err)
	}
	return WriteCTs(outputPath, []*rlwe.Ciphertext{out})
}

// offset-placed CTs -> single ct_R
func (b *Backend) SumResults(inputPaths []string, outPath string) error {
	if len(inputPaths) == 0 {
		return fmt.Errorf("SumResults: no inputs")
	}
	params, err := DefaultParams()
	if err != nil {
		return err
	}
	eval := bgv.NewEvaluator(params, nil)

	var acc *rlwe.Ciphertext
	for _, path := range inputPaths {
		cts, err := ReadCTs(params, path)
		if err != nil {
			return fmt.Errorf("SumResults: load %s: %w", path, err)
		}
		if len(cts) != 1 {
			return fmt.Errorf("SumResults: %s holds %d chunks, want 1", path, len(cts))
		}
		if acc == nil {
			acc = cts[0]
			continue
		}
		alignLevels(eval, acc, cts[0])
		if err := eval.Add(acc, cts[0], acc); err != nil {
			return fmt.Errorf("SumResults: add %s: %w", path, err)
		}
	}
	return WriteCTs(outPath, []*rlwe.Ciphertext{acc})
}
