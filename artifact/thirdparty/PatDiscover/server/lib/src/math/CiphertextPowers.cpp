#include "pdserver/math/CiphertextPowers.hpp"

namespace pat_disc {
    DynamicCiphertextPowers::DynamicCiphertextPowers(const CryptoContext &cc, const Ciphertext &ct)
        : m_CC(cc)
    {
        m_CiphertextPowers.insert({1, ct});
    }

    const Ciphertext &DynamicCiphertextPowers::getPower/* NOLINT(*-no-recursion) */(const uint32_t e)
    {
        if (!m_CiphertextPowers.contains(e))
        {
            long k = 1L << (lbcrypto::NextPowerOfTwo(e) - 1);
            const Ciphertext ctEK = getPower(e - k);
            const Ciphertext ctK = getPower(k);
            m_CiphertextPowers.insert({e, m_CC->EvalMult(ctEK, ctK)});
        }

        return m_CiphertextPowers.at(e);
    }

    bool DynamicCiphertextPowers::isPowerComputed(const uint32_t e) const
    {
        return m_CiphertextPowers.contains(e);
    }
}
