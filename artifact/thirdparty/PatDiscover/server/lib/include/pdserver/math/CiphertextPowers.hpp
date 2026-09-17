#ifndef CIPHERTEXTPOWERS_HPP
#define CIPHERTEXTPOWERS_HPP

namespace pat_disc {
  class DynamicCiphertextPowers
  {
  public:
    explicit DynamicCiphertextPowers(const CryptoContext& cc, const Ciphertext& ct);

    const Ciphertext& getPower(uint32_t e);

    [[nodiscard]] bool isPowerComputed(uint32_t e) const;

  private:
    CryptoContext m_CC;
    std::map<uint32_t, Ciphertext> m_CiphertextPowers;
  };
}

#endif //CIPHERTEXTPOWERS_HPP
