#ifndef SERVER_QUERYPROCESSING_HPP
#define SERVER_QUERYPROCESSING_HPP
#include <NTL/ZZX.h>
#include <filesystem>
#include <pdserver/math/CiphertextPowers.hpp>
#include <pdserver/math/Polynomial.hpp>

namespace pat_disc {
    class QueryProcessing
    {
    public:
        static void Init();
        // FeCaD binding: load polynomial files from an explicit directory
        // instead of the CWD-relative "data/polynomials" default.
        static void Init(const std::filesystem::path &polyDir);

        /// Performs the boolean equality matching
        ///
        /// Calculates equality of booleans x, y using x * y + (1 - x) * (1 - y)
        ///
        /// @param cc The boolean crypto context
        /// @param data The patient data ciphertext
        /// @param query The query ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext BooleanMatching(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query);

        /// Performs the precise enum matching.
        ///
        /// Uses Fermat's Little Theorem and BFV modular arithmetic.
        /// Matches patient and query value if they are equal.
        ///
        /// @param cc The precise enum crypto context
        /// @param data The patient data ciphertext
        /// @param query The query ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        EqualityMatchingPrecise(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query);

        /// Performs the approximate enum matching.
        ///
        /// Uses an approximation of the sign function in CKKS.
        /// Matches patient and query value if they are equal.
        ///
        /// @param cc The approximate enum crypto context
        /// @param data The patient data ciphertext
        /// @param query The query ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        EqualityMatchingApprox(const CryptoContext &cc, const Ciphertext &data, const Ciphertext &query);

        /// Performs the precise range matching.
        ///
        /// Uses scheme switching and the calculation of the sign function in FHEW.
        /// Matches patient and query value if the patient value lies in between \a queryLower and \a queryUpper.
        ///
        /// @param cc The precise comparison crypto context
        /// @param data The patient data ciphertext
        /// @param queryLower The lower bound query ciphertext
        /// @param queryUpper The upper bound query ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        ComparisonMatchingPrecise(const CryptoContext &cc, const Ciphertext &data,
                                  const Ciphertext &queryLower, const Ciphertext &queryUpper);

        /// Performs the approximate range matching.
        ///
        /// Uses an approximation of the less-than function in CKKS.
        /// Matches patient and query value if the patient value lies in between \a queryLower and \a queryUpper.
        ///
        /// @param cc The approx comparison crypto context
        /// @param data The patient data ciphertext
        /// @param queryLower The lower bound query ciphertext
        /// @param queryUpper The upper bound query ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        ComparisonMatchingApprox(const CryptoContext &cc, const Ciphertext &data,
                                 const Ciphertext &queryLower, const Ciphertext &queryUpper);

        /// Performs the precise distance matching.
        ///
        /// Uses scheme switching and the calculation of the sign function in FHEW.
        /// Matches patient and query value if the squared Euclidean distance between data and query point is less than \a queryComparison.
        ///
        /// @param cc The precise distance crypto context
        /// @param dataX The patient data x coordinate ciphertext
        /// @param dataY The patient data y coordinate ciphertext
        /// @param dataZ The patient data z coordinate ciphertext
        /// @param queryX The query x coordinate ciphertext
        /// @param queryY The query y coordinate ciphertext
        /// @param queryZ The query z coordinate ciphertext
        /// @param queryComparison The query upper bound ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        DistanceMatchingPrecise(const CryptoContext &cc, const Ciphertext &dataX,
                                const Ciphertext &dataY, const Ciphertext &dataZ,
                                const Ciphertext &queryX, const Ciphertext &queryY,
                                const Ciphertext &queryZ, const Ciphertext &queryComparison);

        /// Performs the approximate distance matching.
        ///
        /// Uses an approximation of the less-than functin in CKKS.
        /// Matches patient and query value if the squared Euclidean distance between data and query point is less than \a queryComparison.
        ///
        /// @param cc The approximate distance crypto context
        /// @param dataX The patient data x coordinate ciphertext
        /// @param dataY The patient data y coordinate ciphertext
        /// @param dataZ The patient data z coordinate ciphertext
        /// @param queryX The query x coordinate ciphertext
        /// @param queryY The query y coordinate ciphertext
        /// @param queryZ The query z coordinate ciphertext
        /// @param queryComparison The query upper bound ciphertext
        /// @return A ciphertext containing the matching results encoded as zero or one
        static Ciphertext
        DistanceMatchingApprox(const CryptoContext &cc, const Ciphertext &dataX,
                               const Ciphertext &dataY, const Ciphertext &dataZ,
                               const Ciphertext &queryX, const Ciphertext &queryY,
                               const Ciphertext &queryZ, const Ciphertext &queryComparison);

        /// Calculates the sum over all ciphertexts.
        ///
        /// @param cc The crypto context corresponding to the ciphertexts.
        /// @param cts A vector of ciphertexts to sum up.
        /// @return A ciphertext containing the result of the summation.
        static Ciphertext Sum(const CryptoContext &cc, const std::vector<Ciphertext> &cts);

        /// Calculates the logical and over all ciphertexts.
        ///
        /// @param cc The crypto context corresponding to the ciphertexts.
        /// @param cts A vector of ciphertexts to evaluate the logical and on.
        /// @return A ciphertext containing the result of the calculation.
        static Ciphertext And(const CryptoContext &cc, const std::vector<Ciphertext> &cts);

        /// Calculates the logical or over all ciphertexts.
        ///
        /// @param cc The crypto context corresponding to the ciphertexts.
        /// @param cts A vector of ciphertexts to evaluate the logical or on.
        /// @return A ciphertext containing the result of the calculation.
        static Ciphertext Or(const CryptoContext &cc, const std::vector<Ciphertext> &cts);

        /// Inverts the ciphertext (1 - ct)
        ///
        /// @param cc The crypto context
        /// @param ct The ciphertext
        /// @return The negation of ct
        static Ciphertext Not(const CryptoContext &cc, const Ciphertext &ct);

        /// Setter for the number of threads already spawned for the parallel database access.
        ///
        /// @param tCount The number of threads already spawned.
        static void SetThreadCount(int32_t tCount);

        static void CreateUnivariateLessThanPoly(NTL::ZZX &result);

    private:
        static Ciphertext EvalCompareSchemeSwitching(const CryptoContext &cc, const Ciphertext &first, const Ciphertext &second);

        static Ciphertext ApproxCompare(const CryptoContext &cc, const Ciphertext &ct1, const Ciphertext &ct2, uint32_t applyF, uint32_t applyG);

        static Ciphertext ApplyApproxSign(const CryptoContext &cc, const Ciphertext &ct, uint32_t applyF, uint32_t applyG);

        static Ciphertext ApplyApproxF(const CryptoContext &cc, const Ciphertext &ct);

        static Ciphertext ApplyApproxG(const CryptoContext &cc, const Ciphertext &ct);

        static Ciphertext SingleOr(const CryptoContext &cc, const Ciphertext &first, const Ciphertext &second);

        static Ciphertext CalculateDoubleChebyshev(const CryptoContext &cc, uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed);

        static Ciphertext CalculateNextChebyshev(const CryptoContext &cc, const Ciphertext &ct, uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed);

        static Ciphertext CalculateNextOddChebyshev(const CryptoContext &cc, uint32_t n, const std::map<uint32_t, Ciphertext> &precomputed);

        static Ciphertext EvaluateCompositePolynomial(const CryptoContext &cc, const Ciphertext &ct, const std::shared_ptr<CompositePolynomial<double> > &polynomial);

        static Ciphertext OddBabyStepGiantStep(const CryptoContext &cc, const Ciphertext &ct, const std::shared_ptr<Polynomial<double> > &polynomial);

        static Ciphertext RecursiveEval(const CryptoContext &cc, const Polynomial<double> &polynomial, const std::map<uint32_t, Ciphertext> &precomputed, uint32_t m,
                                        uint32_t k, uint32_t level);

        static Ciphertext EvaluateUnivariateLessThanPoly(const CryptoContext &cc, const Ciphertext &x, const Ciphertext &y, const NTL::ZZX &poly);

        static Ciphertext AddIntegerConstant(const Ciphertext &ct, int64_t constant);

        static Ciphertext MultByIntegerConstant(const Ciphertext &ct, int64_t constant);

        static Ciphertext EvaluatePowerOfTwoDegreePoly(const CryptoContext &cc, const NTL::ZZX &poly, uint32_t k,
                                                       DynamicCiphertextPowers &babySteps, DynamicCiphertextPowers &giantSteps);

        static Ciphertext RecursivePolyEval(const CryptoContext &cc, const NTL::ZZX &poly, uint32_t k, DynamicCiphertextPowers &babySteps,
                                            DynamicCiphertextPowers &giantSteps);

        static Ciphertext PatersonStockmeyer(const CryptoContext &cc, const NTL::ZZX &poly, uint32_t k, uint32_t t, uint32_t delta,
                                             DynamicCiphertextPowers &babySteps, DynamicCiphertextPowers &giantSteps);

        static Ciphertext SimplePolyEval(const CryptoContext &cc, const NTL::ZZX &poly, DynamicCiphertextPowers &babySteps);

    private:
        static int32_t s_ThreadCount;

        static std::shared_ptr<CompositePolynomial<double> > s_EnumApproxPoly;
        static std::shared_ptr<CompositePolynomial<double> > s_ContinuousApproxPoly;
        static std::shared_ptr<CompositePolynomial<double> > s_DistanceApproxPoly;

        static NTL::ZZX s_PreciseLessThanPoly;

        static std::map<uint32_t, std::vector<int64_t> > s_PreciseComparisonVectors;
    };
}


#endif //SERVER_QUERYPROCESSING_HPP
