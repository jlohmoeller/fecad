#include "patdiscover_io.hpp"

#include <fstream>
#include <stdexcept>

#include "utils/fsync_util.hpp"

using namespace lbcrypto;

namespace patdiscover {

static void ensure(bool ok, const std::string &msg) {
    if (!ok) throw std::runtime_error(msg);
}

// upstream-matched CKKS context
CryptoContext<DCRTPoly> createContext() {
    CCParams<CryptoContextCKKSRNS> params;
    // upstream ApproxContinuous runs depth 18; AND/OR fusion after
    // ComparisonMatchingApprox needs a little more, and 22 still fits what
    // RingDim 131072 carries at HEStd_128_classic
    params.SetMultiplicativeDepth(22);
    params.SetSecurityLevel(HEStd_128_classic);
    params.SetRingDim(131072);
    params.SetScalingModSize(50);
    params.SetBatchSize(65536);
    // ScalingTechnique left at the OpenFHE 1.2.3 default (FIXEDAUTO) so depth
    // accounting agrees with upstream OddBabyStepGiantStep

    CryptoContext<DCRTPoly> cc = GenCryptoContext(params);
    cc->Enable(PKE);
    cc->Enable(PRE);
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
    }
    fecad::fsyncPath(path);
}

void loadEvalMultKeys(const std::string &path, const CryptoContext<DCRTPoly> &cc) {
    std::ifstream in(path, std::ios::binary);
    ensure(in.is_open(), "open eval mult key file failed: " + path);
    // eval mult keys live in a process-global registry keyed by the secret
    // key's tag; the CGo bridge stays loaded, so an earlier keygen's tag
    // lingers and collides here — each op reloads just the keys it needs
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

void saveCiphertexts(const std::string &path,
                     const std::vector<Ciphertext<DCRTPoly>> &cts) {
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

void loadCiphertexts(const std::string &path,
                     std::vector<Ciphertext<DCRTPoly>> &cts) {
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

}  // namespace patdiscover
