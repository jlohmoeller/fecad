#pragma once
#include "tfhe++.hpp"
#include <limits>

namespace TFHEEksxt {

using namespace TFHEpp;

template<class P>
inline void cross_ikskgen(KeySwitchingKey<P> &ksk,
                          const SecretKey &skA,
                          const SecretKey &skB)
{   
    const auto &domainA = skA.key.get<typename P::domainP>();

    const auto &targetB = skB.key.get<typename P::targetP>();

    for (int l = 0; l < P::domainP::k; l++)
        for (int i = 0; i < P::domainP::n; i++)
            
            for (int j = 0; j < P::t; j++)
                for (uint32_t k = 0; k < (1u << P::basebit) - 1; k++)
                {
                    // torus encoding of s_A[i]*(k+1)*2^{-(j+1)*basebit}
                    auto mu =
                        domainA[l * P::domainP::n + i] *
                        (k + 1) *
                        (1ULL << (std::numeric_limits<
                                      typename P::targetP::T>::digits -
                                  (j + 1) * P::basebit));
                                  
                    ksk[l * P::domainP::n + i][j][k] =
                        tlweSymEncrypt<typename P::targetP>(
                            mu, P::α, targetB);
                }
}

} // namespace TFHEEksxt