#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ENGORGIO/comp.h"
#include "engorgio_io.hpp"

using namespace lbcrypto;
using json = nlohmann::json;

namespace {

const char *kEncryptedDataFile = "encrypted_data.bin";
const char *kResultFile = "result_query.bin";
const char *kMetaFile = "encrypted_meta.json";

struct Condition {
    int colIdx = 0;
    double val = 0.0;
    std::string op;
    std::string logic;
    int64_t maxVal = -1;
};

std::string joinPath(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + file;
    }
    return dir + "/" + file;
}

std::vector<std::string> sortedKeys(const json &row) {
    std::vector<std::string> keys;
    keys.reserve(row.size());
    for (auto it = row.begin(); it != row.end(); ++it) {
        keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> loadSchemaColumns(const std::string &schemaPath) {
    if (schemaPath.empty()) {
        return {};
    }
    std::ifstream in(schemaPath);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open schema: " + schemaPath);
    }
    json s;
    in >> s;
    if (!s.contains("columns")) {
        throw std::runtime_error("schema missing columns: " + schemaPath);
    }
    struct Entry {
        int index;
        std::string name;
    };
    std::vector<Entry> entries;
    for (const auto &col : s["columns"]) {
        entries.push_back({col.at("index").get<int>(), col.at("name").get<std::string>()});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.index < b.index;
    });
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto &e : entries) {
        out.push_back(e.name);
    }
    return out;
}

void writeMeta(const std::string &dir, const std::vector<std::string> &columns, size_t rows, size_t slots) {
    json m;
    m["columns"] = columns;
    m["rows"] = rows;
    m["slots"] = slots;
    std::ofstream out(joinPath(dir, kMetaFile));
    if (!out.is_open()) {
        throw std::runtime_error("cannot write meta file");
    }
    out << m.dump(2) << "\n";
}

void readMeta(const std::string &dir, std::vector<std::string> &columns, size_t &rows, size_t &slots) {
    std::ifstream in(joinPath(dir, kMetaFile));
    if (!in.is_open()) {
        throw std::runtime_error("cannot open meta file");
    }
    json m;
    in >> m;
    columns = m.at("columns").get<std::vector<std::string>>();
    rows = m.at("rows").get<size_t>();
    slots = m.at("slots").get<size_t>();
}

std::vector<Condition> parseInstructions(const std::string &instructions) {
    std::vector<Condition> out;
    std::stringstream ss(instructions);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) {
            continue;
        }
        std::stringstream inner(item);
        std::string seg;
        std::vector<std::string> parts;
        while (std::getline(inner, seg, ',')) {
            parts.push_back(seg);
        }
        if (parts.size() < 4) {
            continue;
        }
        Condition c;
        c.colIdx = std::stoi(parts[0]);
        c.val = std::stod(parts[1]);
        c.op = parts[2];
        c.logic = parts[3];
        if (parts.size() >= 5) {
            c.maxVal = std::stoll(parts[4]);
        }
        out.push_back(c);
    }
    return out;
}

Ciphertext<DCRTPoly> constCipher(const CryptoContext<DCRTPoly> &cc,
                                 const PublicKey<DCRTPoly> &pk,
                                 size_t slots,
                                 double value) {
    std::vector<double> vals(slots, value);
    auto pt = cc->MakeCKKSPackedPlaintext(vals);
    return cc->Encrypt(pk, pt);
}

Ciphertext<DCRTPoly> homOr(const CryptoContext<DCRTPoly> &cc,
                           const Ciphertext<DCRTPoly> &a,
                           const Ciphertext<DCRTPoly> &b) {
    auto sum = cc->EvalAdd(a, b);
    auto prod = cc->EvalMult(a, b);
    return cc->EvalSub(sum, prod);
}

Ciphertext<DCRTPoly> evalCondition(
    const CryptoContext<DCRTPoly> &cc,
    const PublicKey<DCRTPoly> &pk,
    const PrivateKey<DCRTPoly> &sk,
    const Ciphertext<DCRTPoly> &col,
    const Condition &cond,
    size_t slots,
    const Ciphertext<DCRTPoly> &ctZero,
    const Ciphertext<DCRTPoly> &ctOne) {

    const double val = cond.val;
    if (cond.maxVal >= 0) {
        const double maxVal = static_cast<double>(cond.maxVal);
        if (cond.op == "EQ" && (val < 0 || val > maxVal)) {
            return ctZero;
        }
        if (cond.op == "GT") {
            if (val >= maxVal) return ctZero;
            if (val < 0) return ctOne;
        }
        if (cond.op == "GE") {
            if (val <= 0) return ctOne;
            if (val > maxVal) return ctZero;
        }
        if (cond.op == "LT") {
            if (val <= 0) return ctZero;
            if (val > maxVal) return ctOne;
        }
        if (cond.op == "LE") {
            if (val >= maxVal) return ctOne;
            if (val < 0) return ctZero;
        }
    }

    const double precision = (cond.maxVal >= 0) ? (static_cast<double>(cond.maxVal) + 1.0) : 127.0;
    const int polyDegree = 119;

    auto pt = cc->MakeCKKSPackedPlaintext(std::vector<double>(slots, val));
    auto lit = cc->Encrypt(pk, pt);

    Ciphertext<DCRTPoly> res;
    if (cond.op == "EQ") {
        auto litCopy = lit;
        auto colCopy = col;
        openfhe::comp_equal(litCopy, colCopy, precision, polyDegree, res);
        return res;
    }

    Ciphertext<DCRTPoly> gt;
    if (cond.op == "GT") {
        auto colCopy = col;
        auto litCopy = lit;
        auto skCopy = sk;
        openfhe::comp_greater_than(colCopy, litCopy, precision, polyDegree, gt, skCopy);
        return gt;
    }
    if (cond.op == "LT") {
        auto litCopy = lit;
        auto colCopy = col;
        auto skCopy = sk;
        openfhe::comp_greater_than(litCopy, colCopy, precision, polyDegree, gt, skCopy);
        return gt;
    }

    Ciphertext<DCRTPoly> eq;
    if (cond.op == "GE") {
        auto colCopy = col;
        auto litCopy = lit;
        auto skCopy = sk;
        openfhe::comp_greater_than(colCopy, litCopy, precision, polyDegree, gt, skCopy);
        colCopy = col;
        litCopy = lit;
        openfhe::comp_equal(litCopy, colCopy, precision, polyDegree, eq);
        return homOr(cc, gt, eq);
    }
    if (cond.op == "LE") {
        auto litCopy = lit;
        auto colCopy = col;
        auto skCopy = sk;
        openfhe::comp_greater_than(litCopy, colCopy, precision, polyDegree, gt, skCopy);
        litCopy = lit;
        colCopy = col;
        openfhe::comp_equal(litCopy, colCopy, precision, polyDegree, eq);
        return homOr(cc, gt, eq);
    }

    throw std::runtime_error("unknown op: " + cond.op);
}

// compare against encrypted literal
Ciphertext<DCRTPoly> evalConditionWithEncLit(
    const CryptoContext<DCRTPoly> &cc,
    const PrivateKey<DCRTPoly> &sk,
    const Ciphertext<DCRTPoly> &col,
    const Ciphertext<DCRTPoly> &lit,
    const std::string &op,
    double precision) {

    const int polyDegree = 119;
    Ciphertext<DCRTPoly> gt, eq, res;

    if (op == "EQ") {
        auto litC = lit, colC = col;
        openfhe::comp_equal(litC, colC, precision, polyDegree, res);
        return res;
    }
    if (op == "GT") {
        auto colC = col, litC = lit;
        auto skC = sk;
        openfhe::comp_greater_than(colC, litC, precision, polyDegree, gt, skC);
        return gt;
    }
    if (op == "LT") {
        auto litC = lit, colC = col;
        auto skC = sk;
        openfhe::comp_greater_than(litC, colC, precision, polyDegree, gt, skC);
        return gt;
    }
    if (op == "GE") {
        auto colC = col, litC = lit;
        auto skC = sk;
        openfhe::comp_greater_than(colC, litC, precision, polyDegree, gt, skC);
        colC = col; litC = lit;
        openfhe::comp_equal(litC, colC, precision, polyDegree, eq);
        return homOr(cc, gt, eq);
    }
    if (op == "LE") {
        auto litC = lit, colC = col;
        auto skC = sk;
        openfhe::comp_greater_than(litC, colC, precision, polyDegree, gt, skC);
        litC = lit; colC = col;
        openfhe::comp_equal(litC, colC, precision, polyDegree, eq);
        return homOr(cc, gt, eq);
    }
    throw std::runtime_error("unknown op: " + op);
}

}  // namespace

int generate_keys(const std::string &dir) {
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

int encrypt_data(const std::string &dir, const std::string &schemaPath) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(dir, engorgio::kContextFile), cc);

    PublicKey<DCRTPoly> pk;
    engorgio::loadPublicKey(joinPath(dir, engorgio::kPublicKeyFile), pk);

    json data;
    {
        std::ifstream in(joinPath(dir, "data.json"));
        if (!in.is_open()) {
            throw std::runtime_error("cannot open data.json");
        }
        in >> data;
    }
    if (!data.is_array() || data.empty()) {
        throw std::runtime_error("data.json must be a non-empty array");
    }

    std::vector<std::string> columns = loadSchemaColumns(schemaPath);
    if (columns.empty()) {
        columns = sortedKeys(data.at(0));
    }

    const size_t rows = data.size();
    const size_t slots = cc->GetRingDimension() / 2;
    if (rows > slots) {
        std::ostringstream msg;
        msg << "rows(" << rows << ") exceed CKKS slots(" << slots << ")";
        throw std::runtime_error(msg.str());
    }

    std::vector<Ciphertext<DCRTPoly>> cts;
    cts.reserve(columns.size());

    for (const auto &col : columns) {
        std::vector<double> vals(slots, 0.0);
        for (size_t i = 0; i < rows; i++) {
            if (!data[i].contains(col)) {
                throw std::runtime_error("missing column: " + col);
            }
            vals[i] = data[i][col].get<double>();
        }
        auto pt = cc->MakeCKKSPackedPlaintext(vals);
        cts.push_back(cc->Encrypt(pk, pt));
    }

    engorgio::saveCiphertexts(joinPath(dir, kEncryptedDataFile), cts);
    writeMeta(dir, columns, rows, slots);
    return 0;
}

int evaluate_dynamic(const std::string &dir, const std::string &instructions) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(dir, engorgio::kContextFile), cc);

    PrivateKey<DCRTPoly> sk;
    engorgio::loadSecretKey(joinPath(dir, engorgio::kSecretKeyFile), sk);

    PublicKey<DCRTPoly> pk;
    engorgio::loadPublicKey(joinPath(dir, engorgio::kPublicKeyFile), pk);

    engorgio::loadEvalMultKeys(joinPath(dir, engorgio::kEvalMultKeyFile), cc);

    std::vector<Ciphertext<DCRTPoly>> cts;
    engorgio::loadCiphertexts(joinPath(dir, kEncryptedDataFile), cts);

    std::vector<std::string> columns;
    size_t rows = 0;
    size_t slots = 0;
    readMeta(dir, columns, rows, slots);

    if (cts.size() != columns.size()) {
        throw std::runtime_error("ciphertext count does not match column count");
    }

    auto conds = parseInstructions(instructions);
    if (conds.empty()) {
        throw std::runtime_error("empty instructions");
    }

    auto ctZero = constCipher(cc, pk, slots, 0.0);
    auto ctOne = constCipher(cc, pk, slots, 1.0);

    Ciphertext<DCRTPoly> acc;
    bool first = true;

    for (const auto &cond : conds) {
        if (cond.colIdx < 0 || static_cast<size_t>(cond.colIdx) >= cts.size()) {
            throw std::runtime_error("column index out of range");
        }
        auto res = evalCondition(cc, pk, sk, cts[cond.colIdx], cond, slots, ctZero, ctOne);
        if (first || cond.logic == "NONE") {
            acc = res;
            first = false;
            continue;
        }
        if (cond.logic == "AND") {
            acc = cc->EvalMult(acc, res);
        } else if (cond.logic == "OR") {
            acc = homOr(cc, acc, res);
        } else {
            throw std::runtime_error("unknown logic: " + cond.logic);
        }
    }

    engorgio::saveCiphertexts(joinPath(dir, kResultFile), {acc});
    return 0;
}

// forward KSK, researcher → provider
int generate_ksk_rl(const std::string &researcherDir, const std::string &providerDir,
                    const std::string &kskPath) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(researcherDir, engorgio::kContextFile), cc);
    PrivateKey<DCRTPoly> skR, skP;
    engorgio::loadSecretKey(joinPath(researcherDir, engorgio::kSecretKeyFile), skR);
    engorgio::loadSecretKey(joinPath(providerDir, engorgio::kSecretKeyFile), skP);
    auto ksk = cc->KeySwitchGen(skR, skP);
    engorgio::saveEvalKey(kskPath, ksk);
    return 0;
}

// key-switch literal batch
int keyswitch_literals(const std::string &kskPath, const std::string &inPath,
                       const std::string &outPath) {
    EvalKey<DCRTPoly> ksk;
    engorgio::loadEvalKey(kskPath, ksk);
    std::vector<Ciphertext<DCRTPoly>> src;
    engorgio::loadCiphertexts(inPath, src);
    if (src.empty()) throw std::runtime_error("keyswitch_literals: empty input");
    auto cc = src[0]->GetCryptoContext();
    std::vector<Ciphertext<DCRTPoly>> dst;
    dst.reserve(src.size());
    for (const auto &ct : src)
        dst.push_back(cc->KeySwitch(ct, ksk));
    engorgio::saveCiphertexts(outPath, dst);
    return 0;
}

// encrypt literals under researcher key
int encrypt_literals(const std::string &researcherDir, const std::string &valuesJson,
                     const std::string &outPath) {
    auto values = json::parse(valuesJson).get<std::vector<int64_t>>();
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(researcherDir, engorgio::kContextFile), cc);
    PublicKey<DCRTPoly> pk;
    engorgio::loadPublicKey(joinPath(researcherDir, engorgio::kPublicKeyFile), pk);
    const size_t slots = cc->GetRingDimension() / 2;
    std::vector<Ciphertext<DCRTPoly>> cts;
    cts.reserve(values.size());
    for (int64_t v : values) {
        std::vector<double> buf(slots, static_cast<double>(v));
        cts.push_back(cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(buf)));
    }
    engorgio::saveCiphertexts(outPath, cts);
    return 0;
}

// evaluate with encrypted literals
// condition i uses lits[i]
int evaluate_enc_literals(const std::string &dir, const std::string &instructions,
                          const std::string &literalsPath) {
    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(dir, engorgio::kContextFile), cc);
    PrivateKey<DCRTPoly> sk;
    engorgio::loadSecretKey(joinPath(dir, engorgio::kSecretKeyFile), sk);
    PublicKey<DCRTPoly> pk;
    engorgio::loadPublicKey(joinPath(dir, engorgio::kPublicKeyFile), pk);
    engorgio::loadEvalMultKeys(joinPath(dir, engorgio::kEvalMultKeyFile), cc);

    std::vector<Ciphertext<DCRTPoly>> cts;
    engorgio::loadCiphertexts(joinPath(dir, kEncryptedDataFile), cts);

    std::vector<std::string> columns;
    size_t rows = 0, slots = 0;
    readMeta(dir, columns, rows, slots);

    if (cts.size() != columns.size())
        throw std::runtime_error("ciphertext count does not match column count");

    std::vector<Ciphertext<DCRTPoly>> lits;
    engorgio::loadCiphertexts(literalsPath, lits);

    auto conds = parseInstructions(instructions);
    if (conds.empty()) throw std::runtime_error("empty instructions");
    if (lits.size() < conds.size())
        throw std::runtime_error("not enough encrypted literals for query");

    Ciphertext<DCRTPoly> acc;
    bool first = true;
    for (size_t ci = 0; ci < conds.size(); ci++) {
        const auto &cond = conds[ci];
        if (cond.colIdx < 0 || static_cast<size_t>(cond.colIdx) >= cts.size())
            throw std::runtime_error("column index out of range");
        const double precision =
            (cond.maxVal >= 0) ? (static_cast<double>(cond.maxVal) + 1.0) : 127.0;
        auto res = evalConditionWithEncLit(cc, sk, cts[cond.colIdx], lits[ci],
                                           cond.op, precision);
        if (first || cond.logic == "NONE") { acc = res; first = false; continue; }
        if (cond.logic == "AND")      acc = cc->EvalMult(acc, res);
        else if (cond.logic == "OR")  acc = homOr(cc, acc, res);
        else throw std::runtime_error("unknown logic: " + cond.logic);
    }

    engorgio::saveCiphertexts(joinPath(dir, kResultFile), {acc});
    return 0;
}

// write one patient's masks
// providerDir must already hold crypto_context.bin, public_key.bin, blindings/
int append_blinding(const std::string &providerDir, const std::string &patientID,
                    const std::string &rowIndicesJSON, const std::string &rpStr,
                    const std::string &cancelPath) {
    auto rowIndices = json::parse(rowIndicesJSON).get<std::vector<int>>();
    const double rp = std::stod(rpStr);

    CryptoContext<DCRTPoly> cc;
    engorgio::loadContext(joinPath(providerDir, engorgio::kContextFile), cc);
    PublicKey<DCRTPoly> pk;
    engorgio::loadPublicKey(joinPath(providerDir, engorgio::kPublicKeyFile), pk);

    const size_t slots = cc->GetRingDimension() / 2;

    auto makeMask = [&](double val) -> Ciphertext<DCRTPoly> {
        std::vector<double> v(slots, 0.0);
        for (int idx : rowIndices) {
            if (idx >= 0 && static_cast<size_t>(idx) < slots) {
                v[static_cast<size_t>(idx)] = val;
            }
        }
        return cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(v));
    };

    engorgio::saveCiphertexts(joinPath(joinPath(providerDir, "blindings"), patientID + ".bin"),
                              {makeMask(rp)});
    engorgio::saveCiphertexts(cancelPath, {makeMask(-rp)});
    return 0;
}

// add every blinding CT
int apply_blinding(const std::string &providerDir, const std::string &resultPath,
                   const std::string &outPath) {
    std::vector<Ciphertext<DCRTPoly>> result;
    engorgio::loadCiphertexts(resultPath, result);
    if (result.empty()) {
        throw std::runtime_error("apply_blinding: empty result ciphertext");
    }
    auto cc = result[0]->GetCryptoContext();

    namespace fs = std::filesystem;
    for (const auto &entry : fs::directory_iterator(joinPath(providerDir, "blindings"))) {
        if (entry.path().extension() != ".bin") continue;
        std::vector<Ciphertext<DCRTPoly>> mask;
        engorgio::loadCiphertexts(entry.path().string(), mask);
        if (!mask.empty()) {
            result[0] = cc->EvalAdd(result[0], mask[0]);
        }
    }

    engorgio::saveCiphertexts(outPath, result);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  generate <working_dir>" << std::endl;
        std::cerr << "  encrypt <working_dir> [schema_path]" << std::endl;
        std::cerr << "  evaluate_dynamic <working_dir> <instructions>" << std::endl;
        std::cerr << "  append_blinding <provider_dir> <patient_id> <row_indices_json> <rp> <cancel_path>" << std::endl;
        std::cerr << "  apply_blinding <provider_dir> <result_path> <out_path>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "generate") {
            if (argc < 3) {
                std::cerr << "Usage: " << argv[0] << " generate <working_dir>" << std::endl;
                return 1;
            }
            return generate_keys(argv[2]);
        }
        if (mode == "encrypt") {
            if (argc < 3) {
                std::cerr << "Usage: " << argv[0] << " encrypt <working_dir> [schema_path]" << std::endl;
                return 1;
            }
            std::string schemaPath = (argc >= 4) ? argv[3] : "";
            return encrypt_data(argv[2], schemaPath);
        }
        if (mode == "evaluate_dynamic") {
            if (argc < 4) {
                std::cerr << "Usage: " << argv[0] << " evaluate_dynamic <working_dir> <instructions>" << std::endl;
                return 1;
            }
            return evaluate_dynamic(argv[2], argv[3]);
        }
        if (mode == "append_blinding") {
            if (argc < 7) {
                std::cerr << "Usage: " << argv[0]
                          << " append_blinding <provider_dir> <patient_id> <row_indices_json> <rp> <cancel_path>"
                          << std::endl;
                return 1;
            }
            return append_blinding(argv[2], argv[3], argv[4], argv[5], argv[6]);
        }
        if (mode == "apply_blinding") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " apply_blinding <provider_dir> <result_path> <out_path>" << std::endl;
                return 1;
            }
            return apply_blinding(argv[2], argv[3], argv[4]);
        }
        if (mode == "generate_ksk_rl") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " generate_ksk_rl <researcher_dir> <provider_dir> <ksk_path>" << std::endl;
                return 1;
            }
            return generate_ksk_rl(argv[2], argv[3], argv[4]);
        }
        if (mode == "keyswitch_literals") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " keyswitch_literals <ksk_path> <in_path> <out_path>" << std::endl;
                return 1;
            }
            return keyswitch_literals(argv[2], argv[3], argv[4]);
        }
        if (mode == "encrypt_literals") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " encrypt_literals <researcher_dir> <values_json> <out_path>" << std::endl;
                return 1;
            }
            return encrypt_literals(argv[2], argv[3], argv[4]);
        }
        if (mode == "evaluate_enc_literals") {
            if (argc < 5) {
                std::cerr << "Usage: " << argv[0]
                          << " evaluate_enc_literals <data_dir> <instructions> <literals_path>" << std::endl;
                return 1;
            }
            return evaluate_enc_literals(argv[2], argv[3], argv[4]);
        }
    } catch (const std::exception &e) {
        std::cerr << "engorgio_wrapper: " << e.what() << std::endl;
        return 1;
    }

    std::cerr << "Unknown mode: " << mode << std::endl;
    return 1;
}
