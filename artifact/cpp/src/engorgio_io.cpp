#include "engorgio_io.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include "utils/fsync_util.hpp"

using namespace lbcrypto;

namespace engorgio {

static void ensure(bool ok, const std::string &msg) {
    if (!ok) {
        throw std::runtime_error(msg);
    }
}

CryptoContext<DCRTPoly> createContext() {
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecurityLevel(HEStd_128_classic);
#if NATIVEINT == 128
    usint scalingModSize = 78;
    usint firstModSize = 89;
#else
    usint scalingModSize = 50;
    usint firstModSize = 60;
#endif
    parameters.SetSecretKeyDist(SPARSE_TERNARY);
    parameters.SetScalingModSize(scalingModSize);
    parameters.SetFirstModSize(firstModSize);
    // FIXEDAUTO holds the scaling factor across the modulus chain, so a fresh
    // blinding mask adds cleanly onto a deeply evaluated result; FLEXIBLEAUTO
    // leaves them mismatched and collapses the mask to ~0 on EvalAdd
    parameters.SetScalingTechnique(FIXEDAUTO);
    parameters.SetMultiplicativeDepth(27);
    parameters.SetRingDim(262144);  // 131072 slots — supports up to 131072 rows

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    return cc;
}

void saveContext(const std::string &path, const CryptoContext<DCRTPoly> &cc) {
    ensure(Serial::SerializeToFile(path, cc, SerType::BINARY),
           "serialize context failed: " + path);
    fecad::fsyncPath(path);
}

void loadContext(const std::string &path, CryptoContext<DCRTPoly> &cc) {
    CryptoContextFactory<DCRTPoly>::ReleaseAllContexts();
    ensure(Serial::DeserializeFromFile(path, cc, SerType::BINARY),
           "deserialize context failed: " + path);
}

void savePublicKey(const std::string &path, const PublicKey<DCRTPoly> &pk) {
    ensure(Serial::SerializeToFile(path, pk, SerType::BINARY),
           "serialize public key failed: " + path);
    fecad::fsyncPath(path);
}

void loadPublicKey(const std::string &path, PublicKey<DCRTPoly> &pk) {
    ensure(Serial::DeserializeFromFile(path, pk, SerType::BINARY),
           "deserialize public key failed: " + path);
}

void saveSecretKey(const std::string &path, const PrivateKey<DCRTPoly> &sk) {
    ensure(Serial::SerializeToFile(path, sk, SerType::BINARY),
           "serialize secret key failed: " + path);
    fecad::fsyncPath(path);
}

void loadSecretKey(const std::string &path, PrivateKey<DCRTPoly> &sk) {
    ensure(Serial::DeserializeFromFile(path, sk, SerType::BINARY),
           "deserialize secret key failed: " + path);
}

void saveEvalMultKeys(const std::string &path, const CryptoContext<DCRTPoly> &cc) {
    {
        std::ofstream out(path, std::ios::binary);
        ensure(out.is_open(), "open eval mult key file failed: " + path);
        ensure(cc->SerializeEvalMultKey(out, SerType::BINARY),
               "serialize eval mult keys failed: " + path);
        out.flush();
        ensure(out.good(), "flush eval mult keys failed: " + path);
    }  // close before fsync
    fecad::fsyncPath(path);
}

void loadEvalMultKeys(const std::string &path, const CryptoContext<DCRTPoly> &cc) {
    std::ifstream in(path, std::ios::binary);
    ensure(in.is_open(), "open eval mult key file failed: " + path);
    // eval-mult keys live in a process-global registry keyed by keyTag; an
    // earlier keygen in this process registered the same params-derived tag,
    // so deserializing without clearing throws on the duplicate
    CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
    ensure(cc->DeserializeEvalMultKey(in, SerType::BINARY),
           "deserialize eval mult keys failed: " + path);
}

void saveEvalKey(const std::string &path, const EvalKey<DCRTPoly> &ek) {
    ensure(Serial::SerializeToFile(path, ek, SerType::BINARY),
           "serialize eval key failed: " + path);
    fecad::fsyncPath(path);
}

void loadEvalKey(const std::string &path, EvalKey<DCRTPoly> &ek) {
    ensure(Serial::DeserializeFromFile(path, ek, SerType::BINARY),
           "deserialize eval key failed: " + path);
}

void saveCiphertexts(const std::string &path, const std::vector<Ciphertext<DCRTPoly>> &cts) {
    {
        std::ofstream out(path, std::ios::binary);
        ensure(out.is_open(), "open ciphertext file failed: " + path);
        const uint64_t n = static_cast<uint64_t>(cts.size());
        out.write(reinterpret_cast<const char *>(&n), sizeof(n));
        ensure(out.good(), "write ciphertext count failed: " + path);
        for (const auto &ct : cts) {
            Serial::Serialize(ct, out, SerType::BINARY);
            ensure(out.good(), "serialize ciphertext failed: " + path);
        }
        out.flush();
        ensure(out.good(), "flush ciphertexts failed: " + path);
    }
    fecad::fsyncPath(path);
}

void loadCiphertexts(const std::string &path, std::vector<Ciphertext<DCRTPoly>> &cts) {
    std::ifstream in(path, std::ios::binary);
    ensure(in.is_open(), "open ciphertext file failed: " + path);
    uint64_t n = 0;
    in.read(reinterpret_cast<char *>(&n), sizeof(n));
    ensure(in.good(), "read ciphertext count failed: " + path);
    cts.clear();
    cts.reserve(static_cast<size_t>(n));
    for (uint64_t i = 0; i < n; i++) {
        Ciphertext<DCRTPoly> ct;
        Serial::Deserialize(ct, in, SerType::BINARY);
        ensure(in.good(), "deserialize ciphertext failed: " + path);
        cts.push_back(ct);
    }
}

}  // namespace engorgio
