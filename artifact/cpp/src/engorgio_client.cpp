#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engorgio_io.hpp"

using namespace lbcrypto;
using json = nlohmann::json;

namespace {

std::string joinPath(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + file;
    }
    return dir + "/" + file;
}

int generate_key(const std::string &dir) {
    auto cc = engorgio::createContext();
    auto kp = cc->KeyGen();
    if (!kp.good()) {
        std::cerr << "KeyGen failed" << std::endl;
        return 1;
    }

    engorgio::saveContext(joinPath(dir, engorgio::kContextFile), cc);
    engorgio::saveSecretKey(joinPath(dir, engorgio::kSecretKeyFile), kp.secretKey);
    engorgio::savePublicKey(joinPath(dir, engorgio::kPublicKeyFile), kp.publicKey);

    cc->EvalMultKeyGen(kp.secretKey);
    engorgio::saveEvalMultKeys(joinPath(dir, engorgio::kEvalMultKeyFile), cc);

    return 0;
}

int decrypt(const std::string &dir, const std::string &inPath,
            const std::string &outPath, size_t rows) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(dir, engorgio::kContextFile), cc);

    PrivateKey<DCRTPoly> sk;
    engorgio::loadSecretKey(joinPath(dir, engorgio::kSecretKeyFile), sk);

    std::vector<Ciphertext<DCRTPoly>> cts;
    engorgio::loadCiphertexts(inPath, cts);

    std::vector<std::vector<uint64_t>> out;
    out.reserve(cts.size());

    for (const auto &ct : cts) {
        Plaintext pt;
        cc->Decrypt(sk, ct, &pt);
        auto vals = pt->GetCKKSPackedValue();
        const size_t cap = std::min(rows, vals.size());
        std::vector<uint64_t> row(cap);
        for (size_t i = 0; i < cap; i++) {
            double v = vals[i].real();
            if (v < 0.5) {
                row[i] = 0;
            } else {
                row[i] = 1;
            }
        }
        out.push_back(row);
    }

    json j = out;
    std::ofstream o(outPath);
    if (!o.is_open()) {
        std::cerr << "Failed to open output: " << outPath << std::endl;
        return 1;
    }
    o << j.dump(2) << "\n";
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  generate_key <working_dir>" << std::endl;
        std::cerr << "  decrypt <working_dir> <encrypted_data_path> <out_path> <rows>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "generate_key") {
            if (argc < 3) {
                std::cerr << "Usage: " << argv[0] << " generate_key <working_dir>" << std::endl;
                return 1;
            }
            return generate_key(argv[2]);
        }
        if (mode == "decrypt") {
            if (argc < 6) {
                std::cerr << "Usage: " << argv[0]
                          << " decrypt <working_dir> <encrypted_data_path> <out_path> <rows>" << std::endl;
                return 1;
            }
            return decrypt(argv[2], argv[3], argv[4], static_cast<size_t>(std::stoul(argv[5])));
        }
    } catch (const std::exception &e) {
        std::cerr << "engorgio_client: " << e.what() << std::endl;
        return 1;
    }

    std::cerr << "Unknown mode: " << mode << std::endl;
    return 1;
}
