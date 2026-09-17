/* extern "C" wrappers over OpenFHE/Engorgio
   every entry point catches C++ exceptions and hands them back as a malloc'd
   string, so nothing unwinds across the CGo boundary */

#include "engorgio_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ENGORGIO/comp.h"
#include "engorgio_io.hpp"

using namespace lbcrypto;
using json = nlohmann::json;

/* error helpers */

static char *dup_err(const std::string &msg) { return strdup(msg.c_str()); }

#define TRY_BEGIN try {
#define TRY_END(err_ptr)                                                \
    } catch (const std::exception &_e) {                                \
        if (err_ptr) *(err_ptr) = dup_err(_e.what());                   \
        return 1;                                                        \
    } catch (...) {                                                      \
        if (err_ptr) *(err_ptr) = dup_err("unknown C++ exception");     \
        return 1;                                                        \
    }                                                                    \
    return 0;

/* path helper */

static std::string jp(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

/* schema helpers */

static std::vector<std::string> loadSchemaColumns(const std::string &path) {
    if (path.empty()) return {};
    std::ifstream in(path);
    if (!in.is_open())
        throw std::runtime_error("cannot open schema: " + path);
    json s; in >> s;
    if (!s.contains("columns"))
        throw std::runtime_error("schema missing columns: " + path);
    struct Entry { int index; std::string name; };
    std::vector<Entry> entries;
    for (const auto &col : s["columns"])
        entries.push_back({col.at("index").get<int>(),
                           col.at("name").get<std::string>()});
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.index < b.index; });
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto &e : entries) out.push_back(e.name);
    return out;
}

static std::vector<std::string> sortedKeys(const json &row) {
    std::vector<std::string> keys;
    for (auto it = row.begin(); it != row.end(); ++it) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    return keys;
}

/* metadata helpers */

static const char *kMetaFile    = "encrypted_meta.json";
static const char *kEncDataFile = "encrypted_data.bin";
static const char *kResultFile  = "result_query.bin";

static void writeMeta(const std::string &dir,
                      const std::vector<std::string> &cols,
                      size_t rows, size_t slots) {
    json m;
    m["columns"] = cols;
    m["rows"]    = rows;
    m["slots"]   = slots;
    std::ofstream out(jp(dir, kMetaFile));
    if (!out.is_open())
        throw std::runtime_error("cannot write meta: " + jp(dir, kMetaFile));
    out << m.dump(2) << "\n";
}

static void readMeta(const std::string &dir, std::vector<std::string> &cols,
                     size_t &rows, size_t &slots) {
    std::ifstream in(jp(dir, kMetaFile));
    if (!in.is_open())
        throw std::runtime_error("cannot open meta: " + jp(dir, kMetaFile));
    json m; in >> m;
    cols  = m.at("columns").get<std::vector<std::string>>();
    rows  = m.at("rows").get<size_t>();
    slots = m.at("slots").get<size_t>();
}

/* instruction parser */

// anonymous namespace for internal linkage: each bridge .so defines its own
// Condition of a different size, and weak external ~vector<Condition> symbols
// get collapsed across them → destructor walks the wrong stride → SIGSEGV
namespace {

struct Condition {
    int colIdx    = 0;
    double val    = 0.0;
    std::string op;
    std::string logic;
    int64_t maxVal = -1;
};

} // namespace

static std::vector<Condition> parseInstructions(const std::string &s) {
    std::vector<Condition> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        std::stringstream inner(item);
        std::string seg;
        std::vector<std::string> parts;
        while (std::getline(inner, seg, ',')) parts.push_back(seg);
        if (parts.size() < 4) continue;
        Condition c;
        c.colIdx = std::stoi(parts[0]);
        c.val    = std::stod(parts[1]);
        c.op     = parts[2];
        c.logic  = parts[3];
        if (parts.size() >= 5) c.maxVal = std::stoll(parts[4]);
        out.push_back(c);
    }
    return out;
}

/* FHE helpers */

static Ciphertext<DCRTPoly> constCipher(const CryptoContext<DCRTPoly> &cc,
                                        const PublicKey<DCRTPoly> &pk,
                                        size_t slots, double value) {
    std::vector<double> v(slots, value);
    return cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(v));
}

static Ciphertext<DCRTPoly> homOr(const CryptoContext<DCRTPoly> &cc,
                                   const Ciphertext<DCRTPoly> &a,
                                   const Ciphertext<DCRTPoly> &b) {
    return cc->EvalSub(cc->EvalAdd(a, b), cc->EvalMult(a, b));
}

// compare against encrypted literal
static Ciphertext<DCRTPoly> evalConditionWithEncLit(
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

// evaluate with plaintext literal
static Ciphertext<DCRTPoly> evalCondition(
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
        if (cond.op == "EQ" && (val < 0 || val > maxVal)) return ctZero;
        if (cond.op == "GT") {
            if (val >= maxVal) return ctZero;
            if (val < 0)       return ctOne;
        }
        if (cond.op == "GE") {
            if (val <= 0)       return ctOne;
            if (val > maxVal)   return ctZero;
        }
        if (cond.op == "LT") {
            if (val <= 0)       return ctZero;
            if (val > maxVal)   return ctOne;
        }
        if (cond.op == "LE") {
            if (val >= maxVal)  return ctOne;
            if (val < 0)        return ctZero;
        }
    }

    const double precision =
        (cond.maxVal >= 0) ? (static_cast<double>(cond.maxVal) + 1.0) : 127.0;
    auto lit = cc->Encrypt(pk,
        cc->MakeCKKSPackedPlaintext(std::vector<double>(slots, val)));
    return evalConditionWithEncLit(cc, sk, col, lit, cond.op, precision);
}

/* extern "C" implementations */

extern "C" {

void engorgio_free_string(char *s) { free(s); }

/* researcher */

int engorgio_researcher_generate_key(const char *dir, char **err) {
    TRY_BEGIN
        std::string d(dir);
        auto cc = engorgio::createContext();
        auto kp = cc->KeyGen();
        if (!kp.good()) throw std::runtime_error("KeyGen failed");
        engorgio::saveContext(jp(d, engorgio::kContextFile), cc);
        engorgio::saveSecretKey(jp(d, engorgio::kSecretKeyFile), kp.secretKey);
        engorgio::savePublicKey(jp(d, engorgio::kPublicKeyFile), kp.publicKey);
        // SerializeEvalMultKey dumps every tag in the process-global registry
        // and the CGo bridge stays loaded across keygen calls, so without this
        // each provider's tar inherits every prior provider's keys
        CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
        cc->EvalMultKeyGen(kp.secretKey);
        engorgio::saveEvalMultKeys(jp(d, engorgio::kEvalMultKeyFile), cc);
    TRY_END(err)
}

int engorgio_researcher_decrypt(const char *dir, const char *in_path,
                                const char *out_path, int rows, char **err) {
    TRY_BEGIN
        std::string d(dir);
        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(d, engorgio::kContextFile), cc);
        PrivateKey<DCRTPoly> sk;
        engorgio::loadSecretKey(jp(d, engorgio::kSecretKeyFile), sk);

        std::vector<Ciphertext<DCRTPoly>> cts;
        engorgio::loadCiphertexts(std::string(in_path), cts);

        std::vector<std::vector<uint64_t>> out;
        out.reserve(cts.size());
        const size_t cap = static_cast<size_t>(rows);
        for (const auto &ct : cts) {
            Plaintext pt;
            cc->Decrypt(sk, ct, &pt);
            auto vals = pt->GetCKKSPackedValue();
            const size_t n = std::min(cap, vals.size());
            std::vector<uint64_t> row(n);
            // raw rounded residue, not a 0/1 threshold: a blinded row carries
            // predicate + r_p and must stay garbled (≠0, ≠1)
            for (size_t i = 0; i < n; i++) {
                const double v = vals[i].real();
                row[i] = v < 0.0 ? 0u
                                 : static_cast<uint64_t>(std::llround(v));
            }
            out.push_back(row);
        }

        json j = out;
        std::ofstream o(out_path);
        if (!o.is_open())
            throw std::runtime_error("cannot open output: " +
                                     std::string(out_path));
        o << j.dump(2) << "\n";
    TRY_END(err)
}

/* provider */

int engorgio_provider_generate_keys(const char *dir, char **err) {
    TRY_BEGIN
        std::string d(dir);
        auto cc = engorgio::createContext();
        auto kp = cc->KeyGen();
        if (!kp.good()) throw std::runtime_error("KeyGen failed");
        engorgio::saveContext(jp(d, engorgio::kContextFile), cc);
        engorgio::saveSecretKey(jp(d, engorgio::kSecretKeyFile), kp.secretKey);
        engorgio::savePublicKey(jp(d, engorgio::kPublicKeyFile), kp.publicKey);
        // clear the global eval-mult registry first, see
        // engorgio_researcher_generate_key
        CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
        cc->EvalMultKeyGen(kp.secretKey);
        engorgio::saveEvalMultKeys(jp(d, engorgio::kEvalMultKeyFile), cc);
    TRY_END(err)
}

int engorgio_provider_encrypt(const char *dir, const char *schema_path, char **err) {
    TRY_BEGIN
        std::string d(dir);
        std::string sp(schema_path ? schema_path : "");

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(d, engorgio::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        engorgio::loadPublicKey(jp(d, engorgio::kPublicKeyFile), pk);

        json data;
        {
            std::ifstream in(jp(d, "data.json"));
            if (!in.is_open())
                throw std::runtime_error("cannot open data.json in " + d);
            in >> data;
        }
        if (!data.is_array() || data.empty())
            throw std::runtime_error("data.json must be a non-empty array");

        std::vector<std::string> columns = loadSchemaColumns(sp);
        if (columns.empty()) columns = sortedKeys(data.at(0));

        const size_t rows  = data.size();
        const size_t slots = cc->GetRingDimension() / 2;
        if (rows > slots) {
            std::ostringstream m;
            m << "rows(" << rows << ") exceed CKKS slots(" << slots << ")";
            throw std::runtime_error(m.str());
        }

        std::vector<Ciphertext<DCRTPoly>> cts;
        cts.reserve(columns.size());
        for (const auto &col : columns) {
            std::vector<double> vals(slots, 0.0);
            for (size_t i = 0; i < rows; i++) {
                if (!data[i].contains(col))
                    throw std::runtime_error("missing column: " + col);
                vals[i] = data[i][col].get<double>();
            }
            cts.push_back(cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(vals)));
        }

        engorgio::saveCiphertexts(jp(d, kEncDataFile), cts);
        writeMeta(d, columns, rows, slots);
    TRY_END(err)
}

int engorgio_provider_evaluate_dynamic(const char *dir, const char *instructions,
                                        char **err) {
    TRY_BEGIN
        std::string d(dir);

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(d, engorgio::kContextFile), cc);
        PrivateKey<DCRTPoly> sk;
        engorgio::loadSecretKey(jp(d, engorgio::kSecretKeyFile), sk);
        PublicKey<DCRTPoly> pk;
        engorgio::loadPublicKey(jp(d, engorgio::kPublicKeyFile), pk);
        engorgio::loadEvalMultKeys(jp(d, engorgio::kEvalMultKeyFile), cc);

        std::vector<Ciphertext<DCRTPoly>> cts;
        engorgio::loadCiphertexts(jp(d, kEncDataFile), cts);

        std::vector<std::string> columns;
        size_t rows = 0, slots = 0;
        readMeta(d, columns, rows, slots);

        if (cts.size() != columns.size())
            throw std::runtime_error(
                "ciphertext count does not match column count");

        auto conds = parseInstructions(std::string(instructions));
        if (conds.empty()) throw std::runtime_error("empty instructions");

        auto ctZero = constCipher(cc, pk, slots, 0.0);
        auto ctOne  = constCipher(cc, pk, slots, 1.0);

        // AND binds tighter than OR: fold AND-conditions into group, on OR
        // flush group into result via homOr and start a new group
        Ciphertext<DCRTPoly> group, result;
        bool hasGroup = false, hasResult = false;
        for (const auto &cond : conds) {
            if (cond.colIdx < 0 ||
                static_cast<size_t>(cond.colIdx) >= cts.size())
                throw std::runtime_error("column index out of range");
            auto res = evalCondition(cc, pk, sk, cts[cond.colIdx], cond,
                                     slots, ctZero, ctOne);
            if (!hasGroup) {
                group = res; hasGroup = true; continue;
            }
            if (cond.logic == "AND") {
                group = cc->EvalMult(group, res);
            } else if (cond.logic == "OR") {
                if (!hasResult) {
                    result = group; hasResult = true;
                } else {
                    result = homOr(cc, result, group);
                }
                group = res;
            } else {
                throw std::runtime_error("unknown logic: " + cond.logic);
            }
        }
        if (hasGroup) {
            if (!hasResult) result = group;
            else            result = homOr(cc, result, group);
        }

        engorgio::saveCiphertexts(jp(d, kResultFile), {result});
    TRY_END(err)
}

/* proxy */

int engorgio_proxy_generate_ksk(const char *sk_researcher_dir,
                                 const char *sk_provider_dir,
                                 const char *ksk_path, char **err) {
    TRY_BEGIN
        std::string rdir(sk_researcher_dir);
        std::string pdir(sk_provider_dir);

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(pdir, engorgio::kContextFile), cc);
        PrivateKey<DCRTPoly> skP, skR;
        engorgio::loadSecretKey(jp(pdir, engorgio::kSecretKeyFile), skP);
        engorgio::loadSecretKey(jp(rdir, engorgio::kSecretKeyFile), skR);

        // KSK: provider → researcher
        auto ksk = cc->KeySwitchGen(skP, skR);
        engorgio::saveEvalKey(std::string(ksk_path), ksk);
    TRY_END(err)
}

int engorgio_proxy_aggregate(const char *ksk_path, const char *in_path,
                              const char *out_path, char **err) {
    TRY_BEGIN
        EvalKey<DCRTPoly> ksk;
        engorgio::loadEvalKey(std::string(ksk_path), ksk);

        std::vector<Ciphertext<DCRTPoly>> src;
        engorgio::loadCiphertexts(std::string(in_path), src);
        if (src.empty())
            throw std::runtime_error("engorgio_proxy_aggregate: empty input");

        auto cc = src[0]->GetCryptoContext();
        std::vector<Ciphertext<DCRTPoly>> dst;
        dst.reserve(src.size());
        for (const auto &ct : src) {
            auto switched = cc->KeySwitch(ct, ksk);
            // KeySwitch keeps the researcher's key tag, which fails OpenFHE's
            // TypeCheck against provider-encrypted data; the CT decrypts under
            // sk_h now, so it must carry the tag the KSK was generated for
            switched->SetKeyTag(ksk->GetKeyTag());
            dst.push_back(switched);
        }

        engorgio::saveCiphertexts(std::string(out_path), dst);
    TRY_END(err)
}

/* forward KSK (researcher → provider) */

int engorgio_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                    const char *sk_provider_dir,
                                    const char *ksk_path, char **err) {
    TRY_BEGIN
        std::string rdir(sk_researcher_dir);
        std::string pdir(sk_provider_dir);

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(rdir, engorgio::kContextFile), cc);
        PrivateKey<DCRTPoly> skR, skP;
        engorgio::loadSecretKey(jp(rdir, engorgio::kSecretKeyFile), skR);
        engorgio::loadSecretKey(jp(pdir, engorgio::kSecretKeyFile), skP);

        // KSK: researcher → provider
        auto ksk = cc->KeySwitchGen(skR, skP);
        engorgio::saveEvalKey(std::string(ksk_path), ksk);
    TRY_END(err)
}

int engorgio_proxy_keyswitch_literals(const char *ksk_path, const char *in_path,
                                       const char *out_path, char **err) {
    TRY_BEGIN
        EvalKey<DCRTPoly> ksk;
        engorgio::loadEvalKey(std::string(ksk_path), ksk);

        std::vector<Ciphertext<DCRTPoly>> src;
        engorgio::loadCiphertexts(std::string(in_path), src);
        if (src.empty())
            throw std::runtime_error(
                "engorgio_proxy_keyswitch_literals: empty input");

        auto cc = src[0]->GetCryptoContext();
        std::vector<Ciphertext<DCRTPoly>> dst;
        dst.reserve(src.size());
        for (const auto &ct : src) {
            auto switched = cc->KeySwitch(ct, ksk);
            // KeySwitch keeps the researcher's key tag, which fails OpenFHE's
            // TypeCheck against provider-encrypted data; the CT decrypts under
            // sk_h now, so it must carry the tag the KSK was generated for
            switched->SetKeyTag(ksk->GetKeyTag());
            dst.push_back(switched);
        }

        engorgio::saveCiphertexts(std::string(out_path), dst);
    TRY_END(err)
}

/* encrypted literals */

int engorgio_encrypt_literals(const char *researcher_dir, const char *values_json,
                               const char *out_path, char **err) {
    TRY_BEGIN
        std::string rdir(researcher_dir);
        std::vector<int64_t> values =
            json::parse(std::string(values_json)).get<std::vector<int64_t>>();

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(rdir, engorgio::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        engorgio::loadPublicKey(jp(rdir, engorgio::kPublicKeyFile), pk);

        const size_t slots = cc->GetRingDimension() / 2;

        std::vector<Ciphertext<DCRTPoly>> cts;
        cts.reserve(values.size());
        for (int64_t v : values) {
            std::vector<double> buf(slots, static_cast<double>(v));
            cts.push_back(cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(buf)));
        }

        engorgio::saveCiphertexts(std::string(out_path), cts);
    TRY_END(err)
}

int engorgio_provider_evaluate_enc_literals(const char *dir,
                                             const char *instructions,
                                             const char *literals_path,
                                             char **err) {
    TRY_BEGIN
        std::string d(dir);

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(d, engorgio::kContextFile), cc);
        PrivateKey<DCRTPoly> sk;
        engorgio::loadSecretKey(jp(d, engorgio::kSecretKeyFile), sk);
        PublicKey<DCRTPoly> pk;
        engorgio::loadPublicKey(jp(d, engorgio::kPublicKeyFile), pk);
        engorgio::loadEvalMultKeys(jp(d, engorgio::kEvalMultKeyFile), cc);

        std::vector<Ciphertext<DCRTPoly>> cts;
        engorgio::loadCiphertexts(jp(d, kEncDataFile), cts);

        std::vector<std::string> columns;
        size_t rows = 0, slots = 0;
        readMeta(d, columns, rows, slots);

        if (cts.size() != columns.size())
            throw std::runtime_error(
                "ciphertext count does not match column count");

        // condition i uses lits[i]
        std::vector<Ciphertext<DCRTPoly>> lits;
        engorgio::loadCiphertexts(std::string(literals_path), lits);

        auto conds = parseInstructions(std::string(instructions));
        if (conds.empty()) throw std::runtime_error("empty instructions");
        if (lits.size() < conds.size())
            throw std::runtime_error("not enough encrypted literals for query");

        // AND binds tighter than OR, see the plain-literal path
        Ciphertext<DCRTPoly> group, result;
        bool hasGroup = false, hasResult = false;
        for (size_t ci = 0; ci < conds.size(); ci++) {
            const auto &cond = conds[ci];
            if (cond.colIdx < 0 ||
                static_cast<size_t>(cond.colIdx) >= cts.size())
                throw std::runtime_error("column index out of range");

            const double precision =
                (cond.maxVal >= 0) ? (static_cast<double>(cond.maxVal) + 1.0)
                                   : 127.0;
            auto res = evalConditionWithEncLit(cc, sk, cts[cond.colIdx],
                                               lits[ci], cond.op, precision);

            if (!hasGroup) {
                group = res; hasGroup = true; continue;
            }
            if (cond.logic == "AND") {
                group = cc->EvalMult(group, res);
            } else if (cond.logic == "OR") {
                if (!hasResult) {
                    result = group; hasResult = true;
                } else {
                    result = homOr(cc, result, group);
                }
                group = res;
            } else {
                throw std::runtime_error("unknown logic: " + cond.logic);
            }
        }
        if (hasGroup) {
            if (!hasResult) result = group;
            else            result = homOr(cc, result, group);
        }

        engorgio::saveCiphertexts(jp(d, kResultFile), {result});
    TRY_END(err)
}

/* patient blinding */

// blinding value from [2.0, 10.0)
static double sampleBlindingValue() {
    uint64_t u = 0;
    std::ifstream urnd("/dev/urandom", std::ios::binary);
    if (!urnd.read(reinterpret_cast<char *>(&u), sizeof(u)))
        throw std::runtime_error("cannot read /dev/urandom");
    constexpr double invMaxU64 = 1.0 / 18446744073709551616.0;
    return 2.0 + 8.0 * static_cast<double>(u) * invMaxU64;
}

static std::string ctToBytes(const Ciphertext<DCRTPoly> &ct) {
    std::stringstream ss;
    Serial::Serialize(ct, ss, SerType::BINARY);
    return ss.str();
}

static Ciphertext<DCRTPoly> ctFromBytes(const std::string &bytes) {
    std::stringstream ss(bytes);
    Ciphertext<DCRTPoly> ct;
    Serial::Deserialize(ct, ss, SerType::BINARY);
    return ct;
}

// encrypt mask from slot vector
static Ciphertext<DCRTPoly> buildMask(const CryptoContext<DCRTPoly> &cc,
                                      const PublicKey<DCRTPoly> &pk,
                                      const std::vector<double> &slotValues) {
    return cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(slotValues));
}

int engorgio_setup_blinding(const char *provider_dir, const char *proxy_dir,
                            const char *patients_json, char **err) {
    TRY_BEGIN
        std::string pdir(provider_dir), qdir(proxy_dir);
        json patients = json::parse(std::string(patients_json));

        CryptoContext<DCRTPoly> cc;
        engorgio::loadContext(jp(pdir, engorgio::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        engorgio::loadPublicKey(jp(pdir, engorgio::kPublicKeyFile), pk);
        const size_t slots = cc->GetRingDimension() / 2;

        namespace fs = std::filesystem;
        const std::string blindDir = jp(pdir, "blindings");
        const std::string cancDir  = jp(qdir, "cancellations");
        fs::create_directories(blindDir);
        fs::create_directories(cancDir);

        // a CKKS CT at this ring is ~116 MiB, so one per patient is infeasible:
        // +rp masks are packed one CT per chunk, and only (chunk, slots, r_p)
        // metadata is stored for -rp so consent stays per patient
        std::map<int, std::vector<double>> blindAcc;

        // the cancellation store is shared federation-wide, so the stashed
        // public key is named per provider; one shared file left only the last
        // enroller's key and every other provider's rows stayed blinded
        const std::string pkName =
            "public_key_" + std::to_string(std::hash<std::string>{}(pdir)) + ".bin";
        {
            std::ifstream pkIn(jp(pdir, engorgio::kPublicKeyFile),
                               std::ios::binary);
            std::ofstream pkOut(jp(cancDir, pkName),
                                std::ios::binary | std::ios::trunc);
            if (!pkIn.is_open() || !pkOut.is_open())
                throw std::runtime_error(
                    "cannot stash provider pk into proxy dir");
            pkOut << pkIn.rdbuf();
        }
        const std::string idxPath = jp(cancDir, "store.idx");
        std::ofstream idxF(idxPath, std::ios::app);
        if (!idxF.is_open()) throw std::runtime_error("cannot open store.idx");

        for (auto it = patients.begin(); it != patients.end(); ++it) {
            const std::string pid = it.key();
            auto rows = it.value().get<std::vector<int>>();
            if (rows.empty()) continue;

            const double rp = sampleBlindingValue();

            std::map<int, std::vector<int>> byChunk;
            for (int r : rows) {
                if (r < 0) continue;
                byChunk[r / static_cast<int>(slots)].push_back(
                    r % static_cast<int>(slots));
            }
            for (const auto &ce : byChunk) {
                const int chunk = ce.first;
                const auto &slotIdx = ce.second;

                auto &bAcc = blindAcc[chunk];
                if (bAcc.empty()) bAcc.assign(slots, 0.0);
                for (int s : slotIdx)
                    if (s >= 0 && static_cast<size_t>(s) < slots)
                        bAcc[s] = rp;

                // metadata only, no CT yet
                json e = {{"p", pid}, {"c", chunk},
                          {"rp", rp}, {"slots", slotIdx}, {"pk", pkName}};
                idxF << e.dump() << "\n";
            }
        }

        // one packed CT per touched chunk, appended to blindings/chunk_{k}.bin
        // as [uint64 size][CT bytes]
        for (const auto &ca : blindAcc) {
            const std::string path =
                jp(blindDir, "chunk_" + std::to_string(ca.first) + ".bin");
            std::ofstream bf(path, std::ios::binary | std::ios::app);
            if (!bf.is_open())
                throw std::runtime_error("cannot open blinding log: " + path);
            std::string bb = ctToBytes(buildMask(cc, pk, ca.second));
            uint64_t bsz = bb.size();
            bf.write(reinterpret_cast<const char *>(&bsz), sizeof(bsz));
            bf.write(bb.data(), static_cast<std::streamsize>(bb.size()));
        }

        // cancellation CTs are built on demand, nothing else to persist
    TRY_END(err)
}

int engorgio_apply_blinding(const char *blindings_dir, const char *result_path,
                            const char *out_path, char **err) {
    TRY_BEGIN
        std::vector<Ciphertext<DCRTPoly>> result;
        engorgio::loadCiphertexts(std::string(result_path), result);
        if (result.empty())
            throw std::runtime_error("engorgio_apply_blinding: empty result");
        auto cc = result[0]->GetCryptoContext();
        const std::string bdir(blindings_dir);

        for (size_t k = 0; k < result.size(); k++) {
            std::ifstream in(jp(bdir, "chunk_" + std::to_string(k) + ".bin"),
                             std::ios::binary);
            if (!in.is_open()) continue;  // no enrollments touched this chunk
            Ciphertext<DCRTPoly> acc;
            bool have = false;
            while (true) {
                uint64_t sz = 0;
                if (!in.read(reinterpret_cast<char *>(&sz), sizeof(sz))) break;
                std::string buf(sz, '\0');
                if (!in.read(buf.data(), static_cast<std::streamsize>(sz)))
                    throw std::runtime_error("truncated blinding log for chunk " +
                                             std::to_string(k));
                auto ct = ctFromBytes(buf);
                acc = have ? cc->EvalAdd(acc, ct) : ct;
                have = true;
            }
            if (have) result[k] = cc->EvalAdd(result[k], acc);
        }

        engorgio::saveCiphertexts(std::string(out_path), result);
    TRY_END(err)
}

int engorgio_apply_cancellation(const char *result_path, const char *cancel_dir,
                                 const char *consented_json, const char *out_path,
                                 char **err) {
    TRY_BEGIN
        auto consentedVec = json::parse(std::string(consented_json))
                                .get<std::vector<std::string>>();
        std::set<std::string> consented(consentedVec.begin(),
                                        consentedVec.end());

        std::vector<Ciphertext<DCRTPoly>> result;
        engorgio::loadCiphertexts(std::string(result_path), result);
        if (result.empty())
            throw std::runtime_error(
                "engorgio_apply_cancellation: empty result");
        auto cc = result[0]->GetCryptoContext();

        if (!consented.empty()) {
            const std::string cdir(cancel_dir);

            // store.idx holds per-patient metadata; the −r_p mask CT is built
            // in memory and added straight to the result, never written out
            struct Entry {
                int64_t chunk;
                double  rp;
                std::vector<int> slotIdx;
                std::string pkName;
            };
            std::map<std::string, std::vector<Entry>> index;
            {
                std::ifstream idxF(jp(cdir, "store.idx"));
                if (!idxF.is_open())
                    throw std::runtime_error("cannot open store.idx");
                std::string line;
                while (std::getline(idxF, line)) {
                    if (line.empty()) continue;
                    json e = json::parse(line);
                    Entry ent;
                    ent.chunk   = e.at("c").get<int64_t>();
                    ent.rp      = e.at("rp").get<double>();
                    ent.slotIdx = e.at("slots").get<std::vector<int>>();
                    ent.pkName  = e.value("pk", std::string(engorgio::kPublicKeyFile));
                    index[e.at("p").get<std::string>()].push_back(std::move(ent));
                }
            }

            // masks must use the key of the provider that enrolled the
            // patient, so each entry names its own stashed pk
            std::map<std::string, PublicKey<DCRTPoly>> pkCache;
            auto pkFor = [&](const std::string &name) -> const PublicKey<DCRTPoly> & {
                auto it = pkCache.find(name);
                if (it == pkCache.end()) {
                    PublicKey<DCRTPoly> loaded;
                    engorgio::loadPublicKey(jp(cdir, name), loaded);
                    it = pkCache.emplace(name, std::move(loaded)).first;
                }
                return it->second;
            };
            const size_t slots = cc->GetRingDimension() / 2;

            for (const auto &pid : consented) {
                auto it = index.find(pid);
                if (it == index.end()) continue;
                for (const auto &ent : it->second) {
                    if (ent.chunk < 0 ||
                        static_cast<size_t>(ent.chunk) >= result.size())
                        continue;
                    std::vector<double> v(slots, 0.0);
                    for (int s : ent.slotIdx)
                        if (s >= 0 && static_cast<size_t>(s) < slots)
                            v[s] = -ent.rp;
                    auto cancCT = buildMask(cc, pkFor(ent.pkName), v);
                    result[static_cast<size_t>(ent.chunk)] =
                        cc->EvalAdd(result[static_cast<size_t>(ent.chunk)],
                                    cancCT);
                }
            }
        }

        engorgio::saveCiphertexts(std::string(out_path), result);
    TRY_END(err)
}

} /* extern "C" */
