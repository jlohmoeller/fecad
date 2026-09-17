package lattigo

// Homomorphic comparison circuits for BGV over integer plaintext slots

import (
	"fmt"
	"math/big"

	bgvpoly "github.com/tuneinsight/lattigo/v6/circuits/bgv/polynomial"
	"github.com/tuneinsight/lattigo/v6/circuits/common/polynomial"
	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/bgv"
	"github.com/tuneinsight/lattigo/v6/utils/bignum"
)

// all slots = val
func constCT(val uint64, params bgv.Parameters, enc *rlwe.Encryptor, encoder *bgv.Encoder) (*rlwe.Ciphertext, error) {
	vals := make([]uint64, params.MaxSlots())
	for i := range vals {
		vals[i] = val
	}
	pt := bgv.NewPlaintext(params, params.MaxLevel())
	if err := encoder.Encode(vals, pt); err != nil {
		return nil, err
	}
	return enc.EncryptNew(pt)
}

// indicator for (CT_x op val) over {0..maxVal}: slot 1 where it holds
func HomCompare(
	ct *rlwe.Ciphertext,
	val, maxVal int64,
	op string,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	t := int64(params.PlaintextModulus())

	// trivial cases, no FHE ops
	switch op {
	case "GE":
		if val <= 0 {
			return constCT(1, params, enc, encoder)
		}
		if val > maxVal {
			return constCT(0, params, enc, encoder)
		}
	case "LE":
		if val >= maxVal {
			return constCT(1, params, enc, encoder)
		}
		if val < 0 {
			return constCT(0, params, enc, encoder)
		}
	case "GT":
		if val >= maxVal {
			return constCT(0, params, enc, encoder)
		}
		if val < 0 {
			return constCT(1, params, enc, encoder)
		}
	case "LT":
		if val <= 0 {
			return constCT(0, params, enc, encoder)
		}
		if val > maxVal {
			return constCT(1, params, enc, encoder)
		}
	case "EQ":
		if val < 0 || val > maxVal {
			return constCT(0, params, enc, encoder)
		}
	}

	// points where the indicator is 1
	var targets []int64
	switch op {
	case "EQ":
		targets = []int64{val}
	case "GE":
		for i := val; i <= maxVal; i++ {
			targets = append(targets, i)
		}
	case "LE":
		for i := int64(0); i <= val; i++ {
			targets = append(targets, i)
		}
	case "GT":
		for i := val + 1; i <= maxVal; i++ {
			targets = append(targets, i)
		}
	case "LT":
		for i := int64(0); i < val; i++ {
			targets = append(targets, i)
		}
	default:
		return nil, fmt.Errorf("HomCompare: unknown op %q", op)
	}

	// indicator(x) = 1 − complement(x) when the complement is smaller: GE(150, 2000)
	// needs 150 Lagrange bases instead of 1851
	if comp := complementSet(targets, maxVal); len(comp) < len(targets) {
		coeffs, err := lagrangeSum(comp, maxVal, t)
		if err != nil {
			return nil, err
		}
		compResult, err := evalPoly(coeffs, ct, params, eval, enc, encoder)
		if err != nil {
			return nil, err
		}
		one, err := constCT(1, params, enc, encoder)
		if err != nil {
			return nil, err
		}
		alignLevels(eval, one, compResult)
		if err := eval.Sub(one, compResult, one); err != nil {
			return nil, fmt.Errorf("HomCompare complement sub: %w", err)
		}
		return one, nil
	}

	coeffs, err := lagrangeSum(targets, maxVal, t)
	if err != nil {
		return nil, err
	}

	return evalPoly(coeffs, ct, params, eval, enc, encoder)
}

// sum of the Lagrange bases for targets over {0..maxVal}, ascending coefficients
//
// All arithmetic is uint64 mod t: convolving unreduced big.Int factors grew
// coefficients past 22k bits and cost minutes per predicate at maxVal=2047. The
// full product P(x) = ∏(x−i) is built once and each basis follows by synthetic
// division, O(maxVal) per target instead of O(maxVal²)
func lagrangeSum(targets []int64, maxVal, t int64) ([]*big.Int, error) {
	tm := uint64(t)
	full := fullProduct(maxVal, tm)

	acc := make([]uint64, maxVal+1)
	for _, v := range targets {
		basis, err := basisModT(full, v, maxVal, tm)
		if err != nil {
			return nil, err
		}
		for i, c := range basis {
			acc[i] = (acc[i] + c) % tm
		}
	}

	result := make([]*big.Int, maxVal+1)
	for i, c := range acc {
		result[i] = new(big.Int).SetUint64(c)
	}
	return result, nil
}

// coefficients of ∏_{i=0..maxVal} (x − i) mod t, ascending, length maxVal+2
func fullProduct(maxVal int64, t uint64) []uint64 {
	poly := make([]uint64, 1, maxVal+2)
	poly[0] = 1
	for i := int64(0); i <= maxVal; i++ {
		negI := (t - uint64(i)%t) % t // −i mod t
		poly = append(poly, 0)
		// multiply in place by (x − i), descending so poly[k-1] is still the
		// pre-multiplication coefficient when read
		for k := len(poly) - 1; k > 0; k-- {
			poly[k] = (poly[k-1] + poly[k]*negI) % t
		}
		poly[0] = poly[0] * negI % t
	}
	return poly
}

// L_v over {0..maxVal} mod t from the full product: Q(x)/Q(v), Q(x) = P(x)/(x−v)
func basisModT(full []uint64, v, maxVal int64, t uint64) ([]uint64, error) {
	// synthetic division of P by (x − v): Q[k] = P[k+1] + v·Q[k+1]
	vm := uint64(v) % t
	q := make([]uint64, maxVal+1)
	q[maxVal] = full[maxVal+1]
	for k := maxVal - 1; k >= 0; k-- {
		q[k] = (full[k+1] + vm*q[k+1]) % t
	}

	// Q(v) = ∏_{i≠v} (v−i) by Horner
	var qv uint64
	for k := maxVal; k >= 0; k-- {
		qv = (qv*vm + q[k]) % t
	}
	if qv == 0 {
		return nil, fmt.Errorf("lagrangeBasis: zero denominator for v=%d, maxVal=%d", v, maxVal)
	}
	inv := new(big.Int).ModInverse(new(big.Int).SetUint64(qv), new(big.Int).SetUint64(t))
	if inv == nil {
		return nil, fmt.Errorf("lagrangeBasis: denominator not invertible mod t")
	}
	invU := inv.Uint64()
	for k := range q {
		q[k] = q[k] * invU % t
	}
	return q, nil
}

// L_v(x) over {0..maxVal}, coefficients in Z_t
func lagrangeBasis(v, maxVal, t int64) ([]*big.Int, error) {
	basis, err := basisModT(fullProduct(maxVal, uint64(t)), v, maxVal, uint64(t))
	if err != nil {
		return nil, err
	}
	out := make([]*big.Int, len(basis))
	for i, c := range basis {
		out[i] = new(big.Int).SetUint64(c)
	}
	return out, nil
}

// evaluate the polynomial on ct (Paterson-Stockmeyer)
func evalPoly(
	coeffsBig []*big.Int,
	ct *rlwe.Ciphertext,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	coeffs := make([]*bignum.Complex, len(coeffsBig))
	for i, c := range coeffsBig {
		cplx := bignum.NewComplex()
		cplx[0] = new(big.Float).SetPrec(128).SetInt(c)
		cplx[1] = new(big.Float).SetPrec(128).SetInt64(0)
		coeffs[i] = cplx
	}

	bigPoly := bignum.NewPolynomial(bignum.Monomial, coeffs, nil)
	lattPoly := bgvpoly.Polynomial(polynomial.NewPolynomial(bigPoly))

	polyEval := bgvpoly.NewEvaluator(params, eval)
	return polyEval.Evaluate(ct, lattPoly, params.DefaultScale())
}

// Enc([x == v]) with v encrypted and x bounded by {0..maxVal}
//
// d = x − v is then 0 or ±1..±maxVal, so [x = v] = ∏_{i=1..maxVal} (1 − d²/i²):
// depth ⌈log₂(2·maxVal+1)⌉. HomIsZero needs no bound but its 16 squarings leave
// so little budget that an EQ conjoined with two ranges decrypted to zero
func HomEqSmall(
	x, v *rlwe.Ciphertext,
	maxVal int64,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	diff := bgv.NewCiphertext(params, 1, min(x.Level(), v.Level()))
	if err := eval.Sub(x, v, diff); err != nil {
		return nil, fmt.Errorf("HomEqSmall: sub: %w", err)
	}
	coeffs, err := eqZeroPoly(maxVal, int64(params.PlaintextModulus()))
	if err != nil {
		return nil, err
	}
	return evalPoly(coeffs, diff, params, eval, enc, encoder)
}

// coefficients of ∏_{i=1..maxVal} (1 − d²·(i²)⁻¹) in Z_t, ascending in d
// 1 at d=0, 0 at every other difference the domain admits
func eqZeroPoly(maxVal, t int64) ([]*big.Int, error) {
	tm := uint64(t)
	poly := []uint64{1}
	T := new(big.Int).SetUint64(tm)
	for i := int64(1); i <= maxVal; i++ {
		sq := new(big.Int).SetUint64(uint64(i % t * (i % t) % t))
		inv := new(big.Int).ModInverse(sq, T)
		if inv == nil {
			return nil, fmt.Errorf("eqZeroPoly: %d² not invertible mod %d", i, t)
		}
		c := inv.Uint64()
		next := make([]uint64, len(poly)+2)
		copy(next, poly)
		for k, v := range poly {
			next[k+2] = (next[k+2] + tm - v*c%tm) % tm
		}
		poly = next
	}
	out := make([]*big.Int, len(poly))
	for i, c := range poly {
		out[i] = new(big.Int).SetUint64(c)
	}
	return out, nil
}

// Enc([slot == 0]) by Fermat: 1 − x^(t−1) ≡ [x=0] in Z_t, so t−1=2^16 costs 16
// squarings
//
// The multiplication must be scale-invariant: it keeps the scale and the level
// fixed across all 16 squarings, whereas the Rescale-per-squaring chain
// accumulated enough noise to fail decryption from iteration 10 on
func HomIsZero(
	ct *rlwe.Ciphertext,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	t := params.PlaintextModulus()
	if t <= 1 {
		return nil, fmt.Errorf("HomIsZero: t=1 is degenerate")
	}
	exp := t - 1
	if exp == 0 || (exp&(exp-1)) != 0 {
		return nil, fmt.Errorf("HomIsZero: t-1=%d is not a power of 2 (t=%d)", exp, t)
	}

	cur := ct
	for mask := uint64(exp) >> 1; mask > 0; mask >>= 1 {
		var err error
		cur, err = eval.MulRelinScaleInvariantNew(cur, cur)
		if err != nil {
			return nil, fmt.Errorf("HomIsZero: squaring: %w", err)
		}
	}

	one, err := constCT(1, params, enc, encoder)
	if err != nil {
		return nil, err
	}
	alignLevels(eval, one, cur)
	if err := eval.Sub(one, cur, one); err != nil {
		return nil, fmt.Errorf("HomIsZero: sub: %w", err)
	}
	return one, nil
}

// drop both to min level; BGV arithmetic needs matching levels
func alignLevels(eval *bgv.Evaluator, a, b *rlwe.Ciphertext) {
	if d := a.Level() - b.Level(); d > 0 {
		eval.DropLevel(a, d)
	} else if d < 0 {
		eval.DropLevel(b, -d)
	}
}

// slot-wise AND over {0,1}
//
// Scale-invariant mul, matching HomIsZero: mixing it with BGV mul + rescale
// misaligns the scales and the h→u key switch then decrypts to noise
func HomAND(a, b *rlwe.Ciphertext, eval *bgv.Evaluator) (*rlwe.Ciphertext, error) {
	return eval.MulRelinScaleInvariantNew(a, b)
}

// {0..maxVal} \ targets
func complementSet(targets []int64, maxVal int64) []int64 {
	set := make(map[int64]struct{}, len(targets))
	for _, v := range targets {
		set[v] = struct{}{}
	}
	comp := make([]int64, 0, int(maxVal)+1-len(targets))
	for i := int64(0); i <= maxVal; i++ {
		if _, found := set[i]; !found {
			comp = append(comp, i)
		}
	}
	return comp
}

// slot-wise OR as 1 − (1−a)(1−b)
func HomOR(
	a, b *rlwe.Ciphertext,
	params bgv.Parameters,
	eval *bgv.Evaluator,
	enc *rlwe.Encryptor,
	encoder *bgv.Encoder,
) (*rlwe.Ciphertext, error) {
	oneA, err := constCT(1, params, enc, encoder)
	if err != nil {
		return nil, err
	}
	if err := eval.Sub(oneA, a, oneA); err != nil {
		return nil, err
	}
	oneB, err := constCT(1, params, enc, encoder)
	if err != nil {
		return nil, err
	}
	if err := eval.Sub(oneB, b, oneB); err != nil {
		return nil, err
	}
	// scale-invariant mul, as in HomAND
	prod, err := eval.MulRelinScaleInvariantNew(oneA, oneB)
	if err != nil {
		return nil, err
	}
	one, err := constCT(1, params, enc, encoder)
	if err != nil {
		return nil, err
	}
	if err := eval.Sub(one, prod, one); err != nil {
		return nil, err
	}
	return one, nil
}
