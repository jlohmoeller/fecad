#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engorgio_io.hpp"

using json = nlohmann::json;

using namespace lbcrypto;

namespace {

std::string joinPath(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + file;
    }
    return dir + "/" + file;
}

int generate_ksk(const std::string &skResearcherDir,
                 const std::string &skProviderDir,
                 const std::string &outPath) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(skProviderDir, engorgio::kContextFile), cc);

    PrivateKey<DCRTPoly> skProvider;
    PrivateKey<DCRTPoly> skResearcher;
    engorgio::loadSecretKey(joinPath(skProviderDir, engorgio::kSecretKeyFile), skProvider);
    engorgio::loadSecretKey(joinPath(skResearcherDir, engorgio::kSecretKeyFile), skResearcher);

    auto ksk = cc->KeySwitchGen(skProvider, skResearcher);
    engorgio::saveEvalKey(outPath, ksk);
    return 0;
}

int aggregate(const std::string &kskPath, const std::string &inPath, const std::string &outPath) {
    EvalKey<DCRTPoly> ksk;
    engorgio::loadEvalKey(kskPath, ksk);

    std::vector<Ciphertext<DCRTPoly>> src;
    engorgio::loadCiphertexts(inPath, src);
    if (src.empty()) {
        std::cerr << "No ciphertexts to aggregate" << std::endl;
        return 1;
    }

    auto cc = src[0]->GetCryptoContext();
    std::vector<Ciphertext<DCRTPoly>> dst;
    dst.reserve(src.size());
    for (const auto &ct : src) {
        dst.push_back(cc->KeySwitch(ct, ksk));
    }

    engorgio::saveCiphertexts(outPath, dst);
    return 0;
}

}  // namespace

// add consented cancellation CTs
int apply_cancellation(const std::string &cancelDir, const std::string &consentedPath,
                       const std::string &inPath, const std::string &outPath) {
    std::vector<Ciphertext<DCRTPoly>> result;
    engorgio::loadCiphertexts(inPath, result);
    if (result.empty()) {
        throw std::runtime_error("apply_cancellation: empty result ciphertext");
    }
    auto cc = result[0]->GetCryptoContext();

    json consented;
    {
        std::ifstream f(consentedPath);
        if (!f.is_open()) {
            throw std::runtime_error("apply_cancellation: cannot open consented file: " + consentedPath);
        }
        f >> consented;
    }

    for (const auto &patientID : consented.get<std::vector<std::string>>()) {
        std::string ctPath = joinPath(cancelDir, patientID + ".bin");
        std::vector<Ciphertext<DCRTPoly>> cancelCTs;
        engorgio::loadCiphertexts(ctPath, cancelCTs);
        if (!cancelCTs.empty()) {
            result[0] = cc->EvalAdd(result[0], cancelCTs[0]);
        }
    }

    engorgio::saveCiphertexts(outPath, result);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  generate_key <sk_researcher> <sk_provider> <out_path>" << std::endl;
        std::cerr << "  aggregate <ksk_path> <in_file> <out_file>" << std::endl;
        std::cerr << "  apply_cancellation <cancel_dir> <consented_json> <in_file> <out_file>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "generate_key") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " generate_key <sk_researcher> <sk_provider> <out_path>" << std::endl;
                return 1;
            }
            return generate_ksk(argv[2], argv[3], argv[4]);
        }
        if (mode == "aggregate") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " aggregate <ksk_path> <in_file> <out_file>" << std::endl;
                return 1;
            }
            return aggregate(argv[2], argv[3], argv[4]);
        }
        if (mode == "apply_cancellation") {
            if (argc < 6) {
                std::cerr << "Usage: " << argv[0]
                          << " apply_cancellation <cancel_dir> <consented_json> <in_file> <out_file>"
                          << std::endl;
                return 1;
            }
            return apply_cancellation(argv[2], argv[3], argv[4], argv[5]);
        }
    } catch (const std::exception &e) {
        std::cerr << "engorgio_server: " << e.what() << std::endl;
        return 1;
    }

    std::cerr << "Unknown mode: " << mode << std::endl;
    return 1;
}
