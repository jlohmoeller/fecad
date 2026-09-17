#include "pdserver/processing/QueryProcessing.hpp"

#include <pdserver/config/CommandLineParameters.hpp>
#include <pdserver/math/CiphertextPowers.hpp>
#include <pdserver/math/Polynomial.hpp>
#include <pdshared/measure/Timer.hpp>

#include <NTL/ZZX.h>
#include <pdshared/util/Utilities.hpp>

namespace pat_disc {
    int32_t QueryProcessing::s_ThreadCount = 1;
    std::shared_ptr<CompositePolynomial<double> > QueryProcessing::s_EnumApproxPoly = nullptr;
    std::shared_ptr<CompositePolynomial<double> > QueryProcessing::s_ContinuousApproxPoly = nullptr;
    std::shared_ptr<CompositePolynomial<double> > QueryProcessing::s_DistanceApproxPoly = nullptr;
    NTL::ZZX QueryProcessing::s_PreciseLessThanPoly;
    std::map<uint32_t, std::vector<int64_t> > QueryProcessing::s_PreciseComparisonVectors;

    // Thread function for parallelized sign function evaluation
    void SignComputeThread(const lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc, const std::vector<lbcrypto::LWECiphertext> &source,
                           const int32_t start, const int32_t end, std::vector<lbcrypto::LWECiphertext> &dest)
    {
        for (int32_t i = start; i < end; i++)
        {
            dest[i] = cc->GetBinCCForSchemeSwitch()->EvalSign(source[i], true);
        }
    }

    void QueryProcessing::Init()
    {
        Init(std::filesystem::path("data/polynomials"));
    }

    void QueryProcessing::Init(const std::filesystem::path &polyDir)
    {
        s_EnumApproxPoly = CompositePolynomial<double>::Parse((polyDir / "enum_approx.poly").string());
        s_ContinuousApproxPoly = CompositePolynomial<double>::Parse((polyDir / "continuous_approx.poly").string());
        s_DistanceApproxPoly = CompositePolynomial<double>::Parse((polyDir / "distance_approx.poly").string());

        std::ifstream file((polyDir / "bgv-less-than.poly").string());
        if (file.is_open()) file >> s_PreciseLessThanPoly;
    }

    Ciphertext QueryProcessing::BooleanMatching(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query)
    {
        auto ones = cc->MakePackedPlaintext(std::vector<int64_t>(k_BFVBatchSize, 1));

        const auto mult = cc->EvalMult(data, query);
        const auto negData = cc->EvalSub(ones, data);
        const auto negQuery = cc->EvalSub(ones, query);
        const auto queryMult = cc->EvalMult(negData, negQuery);

        return cc->EvalAdd(mult, queryMult);
    }

    Ciphertext QueryProcessing::EqualityMatchingPrecise(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query)
    {
        const auto diff = cc->EvalSub(data, query);
        auto current = diff;

        bool firstMult = true;
        int32_t power = k_BFVPlaintextMod - 1;
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result;
        while (power > 0) // Binary exponentiation
        {
            if (power & 1)
            {
                if (firstMult)
                {
                    result = current;
                    firstMult = false;
                } else
                {
                    result = cc->EvalMult(result, current);
                }
            }
            power >>= 1;

            // Omit last square operation
            if (power > 0)
            {
                current = cc->EvalSquare(current);
            }
        }

        auto ones = cc->MakePackedPlaintext(std::vector<int64_t>(k_BFVBatchSize, 1));
        return cc->EvalSub(ones, result);
    }

    Ciphertext QueryProcessing::EqualityMatchingApprox(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query)
    {
        const auto diff = cc->EvalSub(data, query);
        const Ciphertext sgn = EvaluateCompositePolynomial(cc, diff, s_EnumApproxPoly);

        const auto sq = cc->EvalSquare(sgn);
        return cc->EvalSub(1.0, sq);
    }

    Ciphertext QueryProcessing::ComparisonMatchingPrecise(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &queryLower, const Ciphertext &queryUpper)
    {
        const auto lowerResult = EvaluateUnivariateLessThanPoly(cc, queryLower, data, s_PreciseLessThanPoly);
        const auto upperResult = EvaluateUnivariateLessThanPoly(cc, data, queryUpper, s_PreciseLessThanPoly);
        const auto result = cc->EvalMult(lowerResult, upperResult); // Logical And
        return result;
    }

    Ciphertext QueryProcessing::ComparisonMatchingApprox(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &queryLower, const Ciphertext &queryUpper)
    {
        const auto lowerDiff = cc->EvalSub(data, queryLower);
        const auto upperDiff = cc->EvalSub(queryUpper, data);

        const auto lowerSgn = EvaluateCompositePolynomial(cc, lowerDiff, s_ContinuousApproxPoly);
        const auto upperSgn = EvaluateCompositePolynomial(cc, upperDiff, s_ContinuousApproxPoly);
        const auto lowerSgnSq = cc->EvalSquare(lowerSgn);
        const auto upperSgnSq = cc->EvalSquare(upperSgn);

        // Comparison approximation
        const auto lowerCompare = cc->EvalMult(0.5, cc->EvalAdd(lowerSgn, 1.0));
        const auto upperCompare = cc->EvalMult(0.5, cc->EvalAdd(upperSgn, 1.0));

        // Multiply with sgn^2 to obtain zero result in case of equality
        const auto lowerResult = cc->EvalMult(lowerSgnSq, lowerCompare);
        const auto upperResult = cc->EvalMult(upperSgnSq, upperCompare);

        return cc->EvalMult(lowerResult, upperResult); // Logical And
    }

    Ciphertext QueryProcessing::DistanceMatchingPrecise(const CryptoContext &cc, const Ciphertext &dataX, const Ciphertext &dataY, const Ciphertext &dataZ,
                                                        const Ciphertext &queryX, const Ciphertext &queryY, const Ciphertext &queryZ, const Ciphertext &queryComparison)
    {
        // Calculate squared Euclidean distance
        auto xDiff = cc->EvalSub(dataX, queryX);
        auto yDiff = cc->EvalSub(dataY, queryY);
        auto zDiff = cc->EvalSub(dataZ, queryZ);

        cc->EvalSquareInPlace(xDiff);
        cc->EvalSquareInPlace(yDiff);
        cc->EvalSquareInPlace(zDiff);

        const auto sum = cc->EvalAddMany({xDiff, yDiff, zDiff});
        return EvaluateUnivariateLessThanPoly(cc, sum, queryComparison, s_PreciseLessThanPoly);
    }

    Ciphertext QueryProcessing::DistanceMatchingApprox(const CryptoContext &cc, const Ciphertext &dataX, const Ciphertext &dataY, const Ciphertext &dataZ,
                                                       const Ciphertext &queryX, const Ciphertext &queryY, const Ciphertext &queryZ, const Ciphertext &queryComparison)
    {
        // Calculate squared Euclidean distance
        auto xDiff = cc->EvalSub(dataX, queryX);
        auto yDiff = cc->EvalSub(dataY, queryY);
        auto zDiff = cc->EvalSub(dataZ, queryZ);

        cc->EvalSquareInPlace(xDiff);
        cc->EvalSquareInPlace(yDiff);
        cc->EvalSquareInPlace(zDiff);

        // Use less-than operator for comparison
        const auto sum = cc->EvalAddMany({xDiff, yDiff, zDiff});
        const auto diff = cc->EvalSub(queryComparison, sum);

        Ciphertext sgn = EvaluateCompositePolynomial(cc, diff, s_DistanceApproxPoly);

        const auto sgnSquared = cc->EvalSquare(sgn);
        const auto compare = cc->EvalMult(0.5, cc->EvalAdd(sgn, 1.0)); // Comparison approx

        return cc->EvalMult(sgnSquared, compare); // Multiply with sgn^2 to obtain zero result in case of equality
    }

    Ciphertext QueryProcessing::EvalCompareSchemeSwitching(const CryptoContext &cc, const Ciphertext &first, const Ciphertext &second)
    {
        const auto cDiff = cc->EvalSub(first, second);

        Timer::GetInstance().Switch("Switch CKKS->FHEW");
        const auto LWECiphertexts = cc->EvalCKKStoFHEW(cDiff, k_CKKSSchemeSwitchBatchSize);

        Timer::GetInstance().Switch("BinFHE Eval Sign");
        std::vector<lbcrypto::LWECiphertext> cSigns(LWECiphertexts.size());

        // Use remaining threads for parallelization of sign evaluation
        const int32_t signThreadCount = static_cast<int32_t>(std::thread::hardware_concurrency()) / s_ThreadCount;
        const int32_t batchSize = static_cast<int32_t>(LWECiphertexts.size()) / signThreadCount; // Eval count for each thread

        PD_INFO("Spawning {} threads for sign evaluation.", signThreadCount);

        std::vector<std::thread> threads;
        threads.reserve(signThreadCount);
        {
            int32_t i;
            for (i = 0; i < signThreadCount - 1; i++)
            {
                threads.emplace_back(SignComputeThread, std::ref(cc), std::ref(LWECiphertexts), i * batchSize, (i + 1) * batchSize, std::ref(cSigns));
            }

            // Last thread receives rest of calculations in case number of calculations is not divisible by batchSize
            threads.emplace_back(SignComputeThread, std::ref(cc), std::ref(LWECiphertexts), i * batchSize, LWECiphertexts.size(), std::ref(cSigns));
        }

        for (auto &t: threads)
        {
            t.join();
        }

        Timer::GetInstance().Switch("Switch FHEW->CKKS");
        auto res = cc->EvalFHEWtoCKKS(cSigns, k_CKKSSchemeSwitchBatchSize, k_CKKSSchemeSwitchBatchSize, 4, -1.0, 1.0, 0);
        Timer::GetInstance().Switch("Query Processing");

        return res;
    }

    Ciphertext QueryProcessing::Sum(const CryptoContext &cc, const std::vector<Ciphertext> &cts)
    {
        return cc->EvalAddMany(cts);
    }

    Ciphertext QueryProcessing::And(const CryptoContext &cc, const std::vector<Ciphertext> &cts)
    {
        return cc->EvalMultMany(cts);
    }

    Ciphertext QueryProcessing::Or(const CryptoContext &cc, const std::vector<Ciphertext> &cts)
    {
        if (cts.empty())
        {
            PD_ASSERT(false, "Tried to call OR on empty vector")
        }

        if (cts.size() == 1)
        {
            return cts[0];
        }

        const size_t inSize = cts.size();
        const size_t lim = inSize * 2 - 2;
        std::vector<Ciphertext> ciphertextOrVec;
        ciphertextOrVec.resize(inSize - 1);
        size_t ctrIndex = 0;

        // Binary tree approach
        for (size_t i = 0; i < lim; i = i + 2)
        {
            ciphertextOrVec[ctrIndex] = SingleOr(cc, i < inSize ? cts[i] : ciphertextOrVec[i - inSize],
                                                 i + 1 < inSize ? cts[i + 1] : ciphertextOrVec[i + 1 - inSize]);
            cc->ModReduceInPlace(ciphertextOrVec[ctrIndex++]);
        }

        return ciphertextOrVec.back();
    }

    Ciphertext QueryProcessing::Not(const CryptoContext &cc, const Ciphertext &ct)
    {
        switch (cc->getSchemeId())
        {
            case lbcrypto::SCHEME::BFVRNS_SCHEME:
            {
                auto ones_bfv = cc->MakePackedPlaintext(std::vector<int64_t>(k_BFVBatchSize, 1));
                return cc->EvalSub(ones_bfv, ct);
            }
            case lbcrypto::SCHEME::CKKSRNS_SCHEME:
            {
                auto ones_ckks = cc->MakeCKKSPackedPlaintext(std::vector<double>(k_CKKSBatchSize, 1.0));
                return cc->EvalSub(ones_ckks, ct);
            }
            default:
            {
                PD_ASSERT(false, "Unknown scheme.");
                __builtin_unreachable();
            }
        }
    }

    Ciphertext
    QueryProcessing::ApplyApproxSign(const CryptoContext &cc, const Ciphertext &ct, const uint32_t applyF, const uint32_t applyG)
    {
        // Calculate f_n(g_m(ct))
        lbcrypto::Ciphertext<lbcrypto::DCRTPoly> result = ct;
        for (uint32_t i = 0; i < applyG; i++)
        {
            result = ApplyApproxG(cc, result);
        }

        for (uint32_t i = 0; i < applyF; i++)
        {
            result = ApplyApproxF(cc, result);
        }

        return result;
    }

    void QueryProcessing::SetThreadCount(int32_t const tCount)
    {
        s_ThreadCount = tCount;
    }

    Ciphertext QueryProcessing::ApproxCompare(const CryptoContext &cc, const Ciphertext &ct1, const Ciphertext &ct2, const uint32_t applyF, const uint32_t applyG)
    {
        // Calculate (sgn_a(ct2 - ct1) + 1) / 2.0
        const auto diff = cc->EvalSub(ct2, ct1);
        const auto lbSgn = ApplyApproxSign(cc, diff, applyF, applyG);
        return cc->EvalMult(0.5, cc->EvalAdd(lbSgn, 1.0));
    }

    Ciphertext QueryProcessing::ApplyApproxF(const CryptoContext &cc, const Ciphertext &ct)
    {
        // Calculate f(ct) = -0.5 * ct^3 + 1.5 * ct
        const auto linComp = cc->EvalMult(1.5, ct);
        const auto cubicComp = cc->EvalMult(-0.5, cc->EvalMult(cc->EvalMult(ct, ct), ct));

        return cc->EvalAdd(linComp, cubicComp);
    }

    Ciphertext QueryProcessing::ApplyApproxG(const CryptoContext &cc, const Ciphertext &ct)
    {
        // Calculate g(ct) = -(1358 / 1024) * ct^3 + (2126 / 1024) * ct
        constexpr double firstCoefficient = -1358.0 / static_cast<double>(1 << 10);
        constexpr double secondCoefficient = 2126.0 / static_cast<double>(1 << 10);

        const auto linComp = cc->EvalMult(secondCoefficient, ct);
        const auto cubicComp = cc->EvalMult(firstCoefficient, cc->EvalMult(cc->EvalMult(ct, ct), ct));

        return cc->EvalAdd(linComp, cubicComp);
    }

    Ciphertext QueryProcessing::SingleOr(const CryptoContext &cc, const Ciphertext &first, const Ciphertext &second)
    {
        // Calculate or(a, b) = a + b - a * b
        const auto sum = cc->EvalAdd(first, second);
        const auto prod = cc->EvalMult(first, second);

        return cc->EvalSub(sum, prod);
    }

    Ciphertext QueryProcessing::EvaluateCompositePolynomial(const CryptoContext &cc, const Ciphertext &ct,
                                                            const std::shared_ptr<CompositePolynomial<double> > &polynomial)
    {
        Ciphertext currentResult = ct;
        for (const std::shared_ptr<Polynomial<double> > &poly: *polynomial)
        {
            currentResult = OddBabyStepGiantStep(cc, currentResult, poly);
        }

        return currentResult;
    }

    Ciphertext QueryProcessing::CalculateDoubleChebyshev(const CryptoContext &cc, const uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed)
    {
        const auto sq = cc->EvalSquare(precomputed.at(n / 2));
        return cc->EvalAdd(cc->EvalAdd(sq, sq), -1.0);
    }

    Ciphertext QueryProcessing::CalculateNextChebyshev(const CryptoContext &cc, const Ciphertext &ct, const uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed)
    {
        const auto mul = cc->EvalMult(ct, precomputed.at(n - 1));
        return cc->EvalSub(cc->EvalAdd(mul, mul), precomputed.at(n - 2));
    }

    Ciphertext QueryProcessing::CalculateNextOddChebyshev(const CryptoContext &cc, const uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed)
    {
        const auto mul = cc->EvalMult(precomputed.at(n - 2), precomputed.at(2));
        return cc->EvalSub(cc->EvalAdd(mul, mul), precomputed.at(n - 4));
    }

    Ciphertext QueryProcessing::OddBabyStepGiantStep(const CryptoContext &cc, const Ciphertext &ct, const std::shared_ptr<Polynomial<double> > &polynomial)
    {
        const uint32_t k = 2 * static_cast<uint32_t>(std::round(std::sqrt(polynomial->Degree()) / 2.0));
        const uint32_t m = static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(polynomial->Degree()) / static_cast<double>(k)))) + 1;

        std::map<uint32_t, Ciphertext> precalculatedValues;
        precalculatedValues.insert({1, ct});

        // Powers of two
        precalculatedValues.insert({2, CalculateDoubleChebyshev(cc, 2, precalculatedValues)});
        for (uint32_t i = 4; i <= k; i *= 2)
        {
            precalculatedValues.insert({i, CalculateDoubleChebyshev(cc, i, precalculatedValues)});
        }

        // Odd terms
        if (k > 3)
        {
            precalculatedValues.insert({3, CalculateNextChebyshev(cc, ct, 3, precalculatedValues)});
            for (uint32_t i = 5; i < k; i += 2)
            {
                precalculatedValues.insert({i, CalculateNextOddChebyshev(cc, i, precalculatedValues)});
            }
        }

        // See https://eprint.iacr.org/2020/1549.pdf section 5.4
        if (!precalculatedValues.contains(k))
        {
            const auto firstIndex = static_cast<uint32_t>(std::pow(2, std::floor(std::log2(k))));
            const uint32_t secondIndex = k - firstIndex;
            const uint32_t thirdIndex = static_cast<uint32_t>(std::pow(2, std::floor(std::log2(k)) + 1.0)) - k;

            const auto mul = cc->EvalMult(precalculatedValues[firstIndex], precalculatedValues[secondIndex]);
            const auto doub = cc->EvalAdd(mul, mul);
            if (thirdIndex == 0)
            {
                precalculatedValues.insert({k, cc->EvalAdd(doub, -1.0)});
            } else
            {
                precalculatedValues.insert({
                    k, cc->EvalSub(doub, precalculatedValues[thirdIndex])
                });
            }
        }

        // Giant steps
        for (uint32_t i = 2; i < (1 << (m - 1)) + 1; i *= 2)
        {
            precalculatedValues.insert({i * k, CalculateDoubleChebyshev(cc, i * k, precalculatedValues)});
        }

        return RecursiveEval(cc, *polynomial, precalculatedValues, m, k, 1);
    }

    Ciphertext QueryProcessing::RecursiveEval/* NOLINT(*-no-recursion) */(const CryptoContext &cc, const Polynomial<double> &polynomial,
                                                                          const std::map<uint32_t, Ciphertext> &precomputed, const uint32_t m, const uint32_t k,
                                                                          const uint32_t level)
    {
        if (polynomial.Degree() <= k)
        {
            std::vector<Ciphertext> monomials;
            for (int32_t i = 0; i < polynomial.Size(); i++)
            {
                const double coeff = polynomial.GetAt(i);
                if (coeff == 0.0)
                    continue;

                PD_ASSERT(precomputed.contains(i), "Baby steps does not contain required key");
                monomials.push_back(cc->EvalMult(coeff, precomputed.at(i)));
            }

            return cc->EvalAddMany(monomials);
        }

        Polynomial<double> quotient({});
        Polynomial<double> remainder({});

        const uint32_t currentPower = 1 << (m - level);
        std::vector<double> divCoeffs(currentPower * k + 1, 0);
        divCoeffs[currentPower * k] = 1.0;
        const Polynomial<double> divisor(std::move(divCoeffs));

        polynomial.DivideChebyshev(divisor, quotient, remainder);

        PD_ASSERT(!(quotient.isNull() && remainder.isNull()), "Both quotient and remainder are zero");

        if (quotient.isNull())
        {
            return RecursiveEval(cc, remainder, precomputed, m, k, level + 1);
        }

        if (remainder.isNull())
        {
            const Ciphertext result = RecursiveEval(cc, quotient, precomputed, m, k, level + 1);

            return cc->EvalMult(result, precomputed.at(currentPower * k));
        }

        const Ciphertext quotientRes = RecursiveEval(cc, quotient, precomputed, m, k, level + 1);
        const Ciphertext remainderRes = RecursiveEval(cc, remainder, precomputed, m, k, level + 1);

        return cc->EvalAdd(cc->EvalMult(quotientRes, precomputed.at(currentPower * k)), remainderRes);
    }

    Ciphertext QueryProcessing::RecursivePolyEval/* NOLINT(*-no-recursion) */(const CryptoContext &cc, const NTL::ZZX &poly, const uint32_t k,
                                                                              DynamicCiphertextPowers &babySteps,
                                                                              DynamicCiphertextPowers &giantSteps)
    {
        const uint32_t degree = NTL::deg(poly);

        if (degree <= k)
            return SimplePolyEval(cc, poly, babySteps);

        const long delta = degree % k;
        const long n = std::ceil(static_cast<double>(degree) / static_cast<double>(k));
        long t = 1L << NTL::NextPowerOfTwo(n);

        if (n == t)
            return EvaluatePowerOfTwoDegreePoly(cc, poly, k, babySteps, giantSteps);

        if (n == t - 1 && delta == 0)
            return PatersonStockmeyer(cc, poly, k, t / 2, delta, babySteps, giantSteps);

        t /= 2;

        const long u = degree - k * (t - 1);
        NTL::ZZX r = NTL::trunc(poly, u);
        NTL::ZZX q = NTL::RightShift(poly, u);
        NTL::SetCoeff(r, u);
        q -= 1;

        const Ciphertext ps = PatersonStockmeyer(cc, q, k, t / 2, delta, babySteps, giantSteps);
        Ciphertext giantPower = giantSteps.getPower(u / k);

        if (delta != 0)
            giantPower = cc->EvalMult(giantPower, babySteps.getPower(delta));

        const Ciphertext intermediate = cc->EvalMult(ps, giantPower);
        const Ciphertext rec = RecursivePolyEval(cc, r, k, babySteps, giantSteps);

        return cc->EvalAdd(intermediate, rec);
    }

    void QueryProcessing::CreateUnivariateLessThanPoly(NTL::ZZX &result)
    {
        result = NTL::ZZX(NTL::INIT_MONO, 0, 0);
        NTL::ZZ_p::init(NTL::ZZ(k_BFVPlaintextMod));

        NTL::ZZ_p coeff;
        NTL::ZZ_p fieldElement;

        for (int32_t i = 1; i < k_BFVPlaintextMod - 1; i++)
        {
            coeff = 1;
            for (int32_t a = 2; a <= (k_BFVPlaintextMod - 1) / 2; a++)
            {
                fieldElement = a;
                coeff += NTL::power(fieldElement, k_BFVPlaintextMod - i - 1);
            }

            result += NTL::ZZX(NTL::INIT_MONO, (i - 1) >> 1, NTL::rep(coeff));
        }

        /*return std::make_shared<Polynomial<int32_t> >(std::move(coeffs));

        const uint32_t degree = poly->Degree();

        const auto kk = static_cast<int32_t>(sqrt(degree / 2.0));
        auto babyStepNum = static_cast<int32_t>(1L << lbcrypto::NextPowerOfTwo(kk));

        // heuristic: if #baby_steps >> kk then use a smaler power of two
        if ((babyStepNum == 16 && degree > 167) || (babyStepNum > 16 && babyStepNum > (1.44 * kk)))
            babyStepNum /= 2;

        int32_t giantStepNum = std::ceil(static_cast<double>(degree) / static_cast<double>(babyStepNum));

        int32_t leadCoeff = poly->GetAt(poly->Degree());
        constexpr  int32_t inverseTop = (-8) % k_BFVPlaintextMod;

        int32_t extraCoeff = 0;

        if (giantStepNum != (1L << lbcrypto::NextPowerOfTwo(giantStepNum)))
        {
            if (giantStepNum * babyStepNum != degree)
            {
                leadCoeff = 1;
                extraCoeff = static_cast<int32_t>((1 - poly->GetAt(giantStepNum * babyStepNum)) % k_BFVPlaintextMod);
                poly->SetAt(giantStepNum * babyStepNum, 1);
            }

            if (leadCoeff != 1)
            {
                poly->MultiplyBy(inverseTop);
                poly->ModBy(k_BFVPlaintextMod);
                poly->RemoveLeadingZeros();
            }
        }

        return poly;*/
    }

    Ciphertext QueryProcessing::MultByIntegerConstant(const Ciphertext &ct, const int64_t constant)
    {
        const uint32_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        std::vector<int64_t> &threadVec = s_PreciseComparisonVectors.at(threadId);
        for (int32_t i = 0; i < k_BFVBatchSize; i++)
            threadVec[i] = constant;

        const lbcrypto::Plaintext pt = ct->GetCryptoContext()->MakePackedPlaintext(threadVec);
        return ct->GetCryptoContext()->EvalMult(ct, pt);
    }

    Ciphertext QueryProcessing::EvaluateUnivariateLessThanPoly(const CryptoContext &cc, const Ciphertext &x, const Ciphertext &y, const NTL::ZZX &poly)
    {
        uint32_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        s_PreciseComparisonVectors.insert({threadId, std::vector<int64_t>(k_BFVBatchSize)});

        const uint32_t degree = NTL::deg(poly);

        const auto kk = static_cast<int32_t>(sqrt(degree / 2.0));
        auto babyStepNum = static_cast<int32_t>(1L << NTL::NextPowerOfTwo(kk));

        // heuristic: if #baby_steps >> kk then use a smaller power of two
        if ((babyStepNum == 16 && degree > 167) || (babyStepNum > 16 && babyStepNum > (1.44 * kk)))
            babyStepNum /= 2;

        const int32_t giantStepNum = std::ceil(static_cast<double>(degree) / static_cast<double>(babyStepNum));
        PD_ASSERT(giantStepNum == (1L << NTL::NextPowerOfTwo(giantStepNum)), "Giant Steps are not power of two");

        const Ciphertext z = cc->EvalSub(x, y);
        const Ciphertext z2 = cc->EvalSquare(z);

        DynamicCiphertextPowers babySteps(cc, z2);
        const Ciphertext &zk = babySteps.getPower(babyStepNum);

        DynamicCiphertextPowers giantSteps(cc, zk);
        const Ciphertext polyResult = EvaluatePowerOfTwoDegreePoly(cc, poly, babyStepNum, babySteps, giantSteps);
        const Ciphertext polyResultTimesZ = cc->EvalMult(polyResult, z);

        constexpr int32_t topCoeffDegree = (k_BFVPlaintextMod - 1) >> 1;
        int32_t babyIndex = topCoeffDegree % babyStepNum;
        int32_t giantIndex = topCoeffDegree / babyStepNum;

        if (babyIndex == 0)
        {
            babyIndex = babyStepNum;
            giantIndex -= 1;
        }

        const Ciphertext topTerm = babySteps.getPower(babyIndex);
        const Ciphertext topPower = cc->EvalMult(topTerm, giantSteps.getPower(giantIndex));
        const Ciphertext completeTopTerm = MultByIntegerConstant(topPower, (k_BFVPlaintextMod + 1) >> 1);

        s_PreciseComparisonVectors.erase(threadId);
        return cc->EvalAdd(polyResultTimesZ, completeTopTerm);
    }

    Ciphertext QueryProcessing::AddIntegerConstant(const Ciphertext &ct, const int64_t constant)
    {
        const uint32_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        std::vector<int64_t> &threadVec = s_PreciseComparisonVectors.at(threadId);
        for (int32_t i = 0; i < k_BFVBatchSize; i++)
            threadVec[i] = constant;

        lbcrypto::Plaintext pt = ct->GetCryptoContext()->MakePackedPlaintext(threadVec);
        return ct->GetCryptoContext()->EvalAdd(ct, pt);
    }

    Ciphertext QueryProcessing::EvaluatePowerOfTwoDegreePoly(const CryptoContext &cc, const NTL::ZZX &poly, const uint32_t k,
                                                             DynamicCiphertextPowers &babySteps, DynamicCiphertextPowers &giantSteps)
    {
        if (NTL::deg(poly) <= k)
            return SimplePolyEval(cc, poly, babySteps);

        long n = NTL::deg(poly) / k;
        n = 1L << NTL::NextPowerOfTwo(n);

        NTL::ZZX r = NTL::trunc(poly, (n - 1) * k);
        NTL::ZZX q = NTL::RightShift(poly, (n - 1) * k);
        NTL::SetCoeff(r, (n - 1) * k);
        q -= 1;

        const Ciphertext rResult = PatersonStockmeyer(cc, r, k, n / 2, 0, babySteps, giantSteps);
        const Ciphertext qResult = SimplePolyEval(cc, q, babySteps);

        std::vector<Ciphertext> powers;
        powers.push_back(qResult);
        for (int32_t i = 1; i < n; i *= 2)
            powers.push_back(giantSteps.getPower(i));

        const Ciphertext combinedPowers = cc->EvalMultMany(powers);
        return cc->EvalAdd(rResult, combinedPowers);
    }

    Ciphertext QueryProcessing::PatersonStockmeyer/* NOLINT(*-no-recursion) */(const CryptoContext &cc, const NTL::ZZX &poly, const uint32_t k,
                                                                               const uint32_t t, const uint32_t delta, DynamicCiphertextPowers &babySteps,
                                                                               DynamicCiphertextPowers &giantSteps)
    {
        if (NTL::deg(poly) <= k)
            return SimplePolyEval(cc, poly, babySteps);

        auto r = NTL::trunc(poly, k * t);
        const auto q = NTL::RightShift(poly, k * t);

        const long degQ = NTL::deg(q);
        const NTL::ZZ &coeff = NTL::coeff(r, degQ);
        NTL::SetCoeff(r, degQ, coeff - 1);

        NTL::ZZX c, s;
        NTL::DivRem(c, s, r, q);

        PD_ASSERT(NTL::deg(s) < NTL::deg(q), "Degree of s is not less than degree of q");
        PD_ASSERT(NTL::IsZero(c) || NTL::deg(c) < k - delta, "Nonzero c has not degree smaller than k - delta");

        NTL::SetCoeff(s, degQ);

        const NTL::ZZ p = NTL::to_ZZ(k_BFVPlaintextMod);
        const long degC = NTL::deg(c);
        for (long i = 0; i <= degC; i++)
            rem(c[i], c[i], p);

        const long degS = NTL::deg(s);
        for (long i = 0; i <= degS; i++)
            rem(s[i], s[i], p);

        c.normalize();
        s.normalize();

        const Ciphertext qResult = PatersonStockmeyer(cc, q, k, t / 2, delta, babySteps, giantSteps);
        const Ciphertext cResult = SimplePolyEval(cc, c, babySteps);
        const Ciphertext intermediate = cc->EvalMult(qResult, cc->EvalAdd(cResult, giantSteps.getPower(t)));
        const Ciphertext sResult = PatersonStockmeyer(cc, s, k, t / 2, delta, babySteps, giantSteps);

        return cc->EvalAdd(intermediate, sResult);
    }

    Ciphertext QueryProcessing::SimplePolyEval(const CryptoContext &cc, const NTL::ZZX &poly, DynamicCiphertextPowers &babySteps)
    {
        const long degree = NTL::deg(poly);
        if (degree < 0)
            return MultByIntegerConstant(babySteps.getPower(1), 0);

        const NTL::ZZ p = NTL::to_ZZ(k_BFVPlaintextMod);
        NTL::ZZ coeff;

        std::vector<Ciphertext> monomials;
        for (int32_t i = 1; i <= degree; i++)
        {
            NTL::rem(coeff, NTL::coeff(poly, i), p);

            if (coeff == 0) continue;
            if (coeff > p / 2) coeff -= p;

            const Ciphertext &power = babySteps.getPower(i);
            Ciphertext mono = MultByIntegerConstant(power, NTL::conv<int32_t>(coeff));
            monomials.push_back(std::move(mono));
        }

        Ciphertext result;
        if (!monomials.empty())
        {
            result = cc->EvalAddMany(monomials);
        } else
        {
            result = MultByIntegerConstant(babySteps.getPower(1), 0);
        }

        NTL::rem(coeff, NTL::ConstTerm(poly), p);
        // Add constant term
        if (coeff != 0)
        {
            if (coeff > p / 2) coeff -= p;
            result = AddIntegerConstant(result, NTL::conv<int32_t>(coeff));
        }

        return result;
    }
}
