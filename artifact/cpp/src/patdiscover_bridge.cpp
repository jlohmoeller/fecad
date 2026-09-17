/* extern "C" wrappers over upstream PatDiscover
   real binding: predicate math is pat_disc::QueryProcessing, PRE is OpenFHE's
   ReKeyGen/ReEncrypt; EQ → EqualityMatchingApprox (depth 13), comparisons →
   ComparisonMatchingApprox (depth 18) with a sentinel for the open side */

#include "patdiscover_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "patdiscover_io.hpp"

#include <pdserver/processing/QueryProcessing.hpp>

using namespace lbcrypto;
using json = nlohmann::json;
using pat_disc::QueryProcessing;

/* error helpers */

static char *dup_err(const std::string &msg) { return strdup(msg.c_str()); }

#define TRY_BEGIN try {
#define TRY_END(err_ptr)                                                    \
    } catch (const std::exception &_e) {                                    \
        if (err_ptr) *(err_ptr) = dup_err(_e.what());                       \
        return 1;                                                            \
    } catch (...) {                                                          \
        if (err_ptr) *(err_ptr) = dup_err("unknown C++ exception");         \
        return 1;                                                            \
    }                                                                        \
    return 0;

/* path helper */

static std::string jp(const std::string &dir, const std::string &file) {
    if (!dir.empty() && dir.back() == '/') return dir + file;
    return dir + "/" + file;
}

/* one-shot polynomial init */

#ifndef PATDISCOVER_DEFAULT_POLY_DIR
#define PATDISCOVER_DEFAULT_POLY_DIR ""
#endif

static void initUpstreamOnce() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        const char *envDir = std::getenv("PATDISCOVER_POLY_DIR");
        std::string dir = envDir && *envDir ? envDir : PATDISCOVER_DEFAULT_POLY_DIR;
        if (dir.empty())
            throw std::runtime_error(
                "patdiscover: poly dir not set — export PATDISCOVER_POLY_DIR or "
                "rebuild with PATDISCOVER_DEFAULT_POLY_DIR baked in");
        QueryProcessing::Init(std::filesystem::path(dir));
    });
}

/* schema helpers */

struct ColumnSpec {
    std::string name;
    double minVal = 0.0;
    double maxVal = 1.0;
};

static std::vector<ColumnSpec> loadSchemaSpec(const std::string &path) {
    if (path.empty()) return {};
    std::ifstream in(path);
    if (!in.is_open())
        throw std::runtime_error("cannot open schema: " + path);
    json s; in >> s;
    if (!s.contains("columns"))
        throw std::runtime_error("schema missing columns: " + path);
    struct Entry { int index; ColumnSpec spec; };
    std::vector<Entry> entries;
    for (const auto &col : s["columns"]) {
        Entry e;
        e.index = col.at("index").get<int>();
        e.spec.name = col.at("name").get<std::string>();
        if (col.contains("range") && col["range"].is_array() && col["range"].size() == 2) {
            e.spec.minVal = col["range"][0].get<double>();
            e.spec.maxVal = col["range"][1].get<double>();
        }
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.index < b.index; });
    std::vector<ColumnSpec> out;
    out.reserve(entries.size());
    for (auto &e : entries) out.push_back(std::move(e.spec));
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
                      const std::vector<ColumnSpec> &cols,
                      size_t rows, size_t slots, size_t chunks) {
    json m;
    json colsJ = json::array();
    for (const auto &c : cols) {
        colsJ.push_back({{"name", c.name},
                         {"min", c.minVal},
                         {"max", c.maxVal}});
    }
    m["columns"] = colsJ;
    m["rows"]    = rows;
    m["slots"]   = slots;
    m["chunks"]  = chunks;
    std::ofstream out(jp(dir, kMetaFile));
    if (!out.is_open())
        throw std::runtime_error("cannot write meta: " + jp(dir, kMetaFile));
    out << m.dump(2) << "\n";
}

static void readMeta(const std::string &dir, std::vector<ColumnSpec> &cols,
                     size_t &rows, size_t &slots, size_t &chunks) {
    std::ifstream in(jp(dir, kMetaFile));
    if (!in.is_open())
        throw std::runtime_error("cannot open meta: " + jp(dir, kMetaFile));
    json m; in >> m;
    cols.clear();
    for (const auto &c : m.at("columns")) {
        ColumnSpec s;
        s.name   = c.at("name").get<std::string>();
        s.minVal = c.value("min", 0.0);
        s.maxVal = c.value("max", 1.0);
        cols.push_back(std::move(s));
    }
    rows   = m.at("rows").get<size_t>();
    slots  = m.at("slots").get<size_t>();
    chunks = m.at("chunks").get<size_t>();
}

/* condition parser */

// anonymous namespace for internal linkage: each bridge .so defines its own
// Condition of a different size, and weak external ~vector<Condition> symbols
// get collapsed across them → destructor walks the wrong stride → SIGSEGV
namespace {

struct Condition {
    int colIdx    = 0;
    double val    = 0.0;
    std::string op;
    std::string logic;
    int64_t maxVal = -1;  // unused for approx path, kept for ABI compat
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

/* normalization + encoding helpers */

// normalize into [0.2, 1.0]
// shifted off the polynomial's [-1.2, 1.2] Chebyshev boundary so the 0.1/1.1
// sentinels keep both diffs inside, and the ≥ kBoundaryEps gap always resolves
// through the sgn approximation (k_ContinuousApproxMinDiff = 0.008)
static double normVal(double v, double minV, double maxV) {
    if (maxV <= minV) return 0.2;
    double n = (v - minV) / (maxV - minV);
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n * 0.8 + 0.2;
}

static Ciphertext<DCRTPoly> encPacked(const CryptoContext<DCRTPoly> &cc,
                                       const PublicKey<DCRTPoly> &pk,
                                       const std::vector<double> &v) {
    return cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(v));
}

static Ciphertext<DCRTPoly> encConst(const CryptoContext<DCRTPoly> &cc,
                                      const PublicKey<DCRTPoly> &pk,
                                      size_t slots, double value) {
    std::vector<double> v(slots, value);
    return encPacked(cc, pk, v);
}

// epsilon nudge for >= / <=
// just above k_ContinuousApproxMinDiff (0.008) so sgn resolves the boundary
static constexpr double kBoundaryEps = 0.012;

// dispatch one condition
// result slot ≈ 1 where the predicate holds, ≈ 0 otherwise
static Ciphertext<DCRTPoly> evalCondition(
    const CryptoContext<DCRTPoly> &cc,
    const PublicKey<DCRTPoly> &pk,
    const Ciphertext<DCRTPoly> &col,
    const ColumnSpec &spec,
    const std::string &op,
    double val,
    size_t slots)
{
    const double normLit = normVal(val, spec.minVal, spec.maxVal);

    if (op == "EQ") {
        const auto query = encConst(cc, pk, slots, normLit);
        return QueryProcessing::EqualityMatchingApprox(cc, col, query);
    }
    // single-sided comparisons use sentinels (0.1 / 1.1), outside the data
    // range [0.2, 1.0] but inside the Chebyshev interval [-1.2, 1.2]; ≥/≤ widen
    // by kBoundaryEps so data == val counts as inside
    if (op == "LT") {
        const auto lower = encConst(cc, pk, slots, 0.1);
        const auto upper = encConst(cc, pk, slots, normLit);
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    if (op == "LE") {
        const auto lower = encConst(cc, pk, slots, 0.1);
        const auto upper = encConst(cc, pk, slots, normLit + kBoundaryEps);
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    if (op == "GT") {
        const auto lower = encConst(cc, pk, slots, normLit);
        const auto upper = encConst(cc, pk, slots, 1.1);
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    if (op == "GE") {
        const auto lower = encConst(cc, pk, slots, normLit - kBoundaryEps);
        const auto upper = encConst(cc, pk, slots, 1.1);
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    throw std::runtime_error("patdiscover: unsupported op: " + op);
}

// dispatch with encrypted literal
static Ciphertext<DCRTPoly> evalConditionEncLit(
    const CryptoContext<DCRTPoly> &cc,
    const PublicKey<DCRTPoly> &pk,
    const Ciphertext<DCRTPoly> &col,
    const Ciphertext<DCRTPoly> &lit,
    const std::string &op,
    size_t slots)
{
    if (op == "EQ") {
        return QueryProcessing::EqualityMatchingApprox(cc, col, lit);
    }
    if (op == "LT" || op == "LE") {
        const auto lower = encConst(cc, pk, slots, 0.1);
        // LE includes the boundary: same epsilon as the plaintext-literal path
        const auto upper = (op == "LE") ? cc->EvalAdd(lit, kBoundaryEps) : lit;
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    if (op == "GT" || op == "GE") {
        const auto upper = encConst(cc, pk, slots, 1.1);
        // GE includes the boundary, mirroring the plaintext-literal path
        const auto lower = (op == "GE") ? cc->EvalSub(lit, kBoundaryEps) : lit;
        return QueryProcessing::ComparisonMatchingApprox(cc, col, lower, upper);
    }
    throw std::runtime_error("patdiscover: unsupported op for enc-literal: " + op);
}

/* blinding helpers */

// uniform real in [2.0, 4.0)
// wide enough that round(bit + r) stays clear of {0, 1}, so a blinded row
// decrypts to a value the proxy recognises as non-canonical
static double sampleBlindingValue() {
    uint64_t u = 0;
    std::ifstream urnd("/dev/urandom", std::ios::binary);
    if (!urnd.read(reinterpret_cast<char *>(&u), sizeof(u)))
        throw std::runtime_error("cannot read /dev/urandom");
    return 2.0 + static_cast<double>(u % 2'000'000ULL) / 1'000'000.0;
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

// mask CT: val at slotIdx, 0 elsewhere
static Ciphertext<DCRTPoly> buildMask(const CryptoContext<DCRTPoly> &cc,
                                      const PublicKey<DCRTPoly> &pk,
                                      size_t slots,
                                      const std::vector<int> &slotIdx,
                                      double val) {
    std::vector<double> v(slots, 0.0);
    for (int s : slotIdx)
        if (s >= 0 && static_cast<size_t>(s) < slots) v[static_cast<size_t>(s)] = val;
    return encPacked(cc, pk, v);
}

/* extern "C" implementations */

extern "C" {

void patdiscover_free_string(char *s) { free(s); }

/* researcher */

int patdiscover_researcher_generate_key(const char *dir, char **err) {
    TRY_BEGIN
        std::string d(dir);
        auto cc = patdiscover::createContext();
        auto kp = cc->KeyGen();
        if (!kp.good()) throw std::runtime_error("KeyGen failed");
        patdiscover::saveContext(jp(d, patdiscover::kContextFile), cc);
        patdiscover::saveSecretKey(jp(d, patdiscover::kSecretKeyFile), kp.secretKey);
        patdiscover::savePublicKey(jp(d, patdiscover::kPublicKeyFile), kp.publicKey);
        // SerializeEvalMultKey dumps every tag in the process-global registry
        // and the CGo bridge stays loaded across keygen calls, so without this
        // each provider's tar inherits every prior provider's keys
        CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
        cc->EvalMultKeyGen(kp.secretKey);
        patdiscover::saveEvalMultKeys(jp(d, patdiscover::kEvalMultKeyFile), cc);
    TRY_END(err)
}

int patdiscover_researcher_decrypt(const char *dir, const char *in_path,
                                   const char *out_path, int rows, char **err) {
    TRY_BEGIN
        std::string d(dir);
        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(d, patdiscover::kContextFile), cc);
        PrivateKey<DCRTPoly> sk;
        patdiscover::loadSecretKey(jp(d, patdiscover::kSecretKeyFile), sk);

        std::vector<Ciphertext<DCRTPoly>> cts;
        patdiscover::loadCiphertexts(std::string(in_path), cts);

        // raw rounded residue, not a 0/1 threshold: a blinded row carries
        // bit + r_p and must surface as a non-{0,1} integer so consent
        // filtering can spot it downstream
        const size_t cap = static_cast<size_t>(rows);
        std::vector<uint64_t> bits;
        bits.reserve(cap);
        for (const auto &ct : cts) {
            if (bits.size() >= cap) break;
            Plaintext pt;
            cc->Decrypt(sk, ct, &pt);
            const auto vals = pt->GetCKKSPackedValue();
            for (size_t i = 0; i < vals.size() && bits.size() < cap; i++) {
                const double v = vals[i].real();
                bits.push_back(v < 0.0 ? 0u
                                       : static_cast<uint64_t>(std::llround(v)));
            }
        }

        std::vector<std::vector<uint64_t>> out{bits};
        json j = out;
        std::ofstream o(out_path);
        if (!o.is_open())
            throw std::runtime_error("cannot open output: " + std::string(out_path));
        o << j.dump(2) << "\n";
    TRY_END(err)
}

/* provider */

int patdiscover_provider_generate_keys(const char *dir, char **err) {
    TRY_BEGIN
        std::string d(dir);
        auto cc = patdiscover::createContext();
        auto kp = cc->KeyGen();
        if (!kp.good()) throw std::runtime_error("KeyGen failed");
        patdiscover::saveContext(jp(d, patdiscover::kContextFile), cc);
        patdiscover::saveSecretKey(jp(d, patdiscover::kSecretKeyFile), kp.secretKey);
        patdiscover::savePublicKey(jp(d, patdiscover::kPublicKeyFile), kp.publicKey);
        // clear the global eval-mult registry first, see
        // patdiscover_researcher_generate_key
        CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
        cc->EvalMultKeyGen(kp.secretKey);
        patdiscover::saveEvalMultKeys(jp(d, patdiscover::kEvalMultKeyFile), cc);
    TRY_END(err)
}

int patdiscover_provider_encrypt(const char *dir, const char *schema_path, char **err) {
    TRY_BEGIN
        std::string d(dir);
        std::string sp(schema_path ? schema_path : "");

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(d, patdiscover::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        patdiscover::loadPublicKey(jp(d, patdiscover::kPublicKeyFile), pk);

        json data;
        {
            std::ifstream in(jp(d, "data.json"));
            if (!in.is_open())
                throw std::runtime_error("cannot open data.json in " + d);
            in >> data;
        }
        if (!data.is_array() || data.empty())
            throw std::runtime_error("data.json must be a non-empty array");

        std::vector<ColumnSpec> cols = loadSchemaSpec(sp);
        if (cols.empty()) {
            // no schema: derive names from row 0 and scan for per-column
            // (min, max); schema-driven paths use the declared range instead,
            // for cross-shard consistency
            for (const auto &k : sortedKeys(data.at(0)))
                cols.push_back({k, 0.0, 0.0});
            for (auto &c : cols) {
                double mn = std::numeric_limits<double>::infinity();
                double mx = -std::numeric_limits<double>::infinity();
                for (const auto &row : data) {
                    if (!row.contains(c.name)) continue;
                    const double v = row[c.name].get<double>();
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                c.minVal = std::isfinite(mn) ? mn : 0.0;
                c.maxVal = std::isfinite(mx) ? mx : 1.0;
                if (c.maxVal <= c.minVal) c.maxVal = c.minVal + 1.0;
            }
        }

        const size_t rows   = data.size();
        const size_t slots  = cc->GetEncodingParams()->GetBatchSize();
        const size_t chunks = (rows + slots - 1) / slots;

        // column-major chunked layout: cts[col * chunks + chunk]
        std::vector<Ciphertext<DCRTPoly>> cts;
        cts.reserve(cols.size() * chunks);
        for (const auto &col : cols) {
            for (size_t ch = 0; ch < chunks; ch++) {
                // padding slots take the midpoint 0.6 of [0.2, 1.0]: a literal
                // 0.0 against a 1.1 sentinel leaves the sgn polynomial's fit and
                // the divergence contaminates every slot through the shared scale
                std::vector<double> vals(slots, 0.6);
                const size_t base = ch * slots;
                for (size_t i = 0; i < slots && base + i < rows; i++) {
                    const auto &rec = data[base + i];
                    if (!rec.contains(col.name))
                        throw std::runtime_error("missing column: " + col.name);
                    vals[i] = normVal(rec[col.name].get<double>(),
                                      col.minVal, col.maxVal);
                }
                cts.push_back(encPacked(cc, pk, vals));
            }
        }

        patdiscover::saveCiphertexts(jp(d, kEncDataFile), cts);
        writeMeta(d, cols, rows, slots, chunks);
    TRY_END(err)
}

int patdiscover_provider_evaluate(const char *dir, const char *instructions,
                                   char **err) {
    TRY_BEGIN
        initUpstreamOnce();

        std::string d(dir);

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(d, patdiscover::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        patdiscover::loadPublicKey(jp(d, patdiscover::kPublicKeyFile), pk);
        patdiscover::loadEvalMultKeys(jp(d, patdiscover::kEvalMultKeyFile), cc);

        std::vector<Ciphertext<DCRTPoly>> cts;
        patdiscover::loadCiphertexts(jp(d, kEncDataFile), cts);

        std::vector<ColumnSpec> cols;
        size_t rows = 0, slots = 0, chunks = 0;
        readMeta(d, cols, rows, slots, chunks);

        if (cts.size() != cols.size() * chunks)
            throw std::runtime_error("ciphertext count does not match columns×chunks");

        auto conds = parseInstructions(std::string(instructions));
        if (conds.empty()) throw std::runtime_error("empty instructions");

        // AND binds tighter than OR: fold AND-conditions into group, on OR
        // flush group into result and start a new group
        std::vector<Ciphertext<DCRTPoly>> group, result;
        bool hasResult = false;
        for (const auto &cond : conds) {
            if (cond.colIdx < 0 ||
                static_cast<size_t>(cond.colIdx) >= cols.size())
                throw std::runtime_error("column index out of range");

            std::vector<Ciphertext<DCRTPoly>> res(chunks);
            for (size_t ch = 0; ch < chunks; ch++) {
                const auto &colCt =
                    cts[static_cast<size_t>(cond.colIdx) * chunks + ch];
                res[ch] = evalCondition(cc, pk, colCt,
                                        cols[static_cast<size_t>(cond.colIdx)],
                                        cond.op, cond.val, slots);
            }

            if (group.empty()) {
                group = std::move(res); continue;
            }
            if (cond.logic == "AND") {
                for (size_t ch = 0; ch < chunks; ch++)
                    group[ch] = QueryProcessing::And(cc, {group[ch], res[ch]});
            } else if (cond.logic == "OR") {
                if (!hasResult) {
                    result = std::move(group); hasResult = true;
                } else {
                    for (size_t ch = 0; ch < chunks; ch++)
                        result[ch] = QueryProcessing::Or(cc, {result[ch], group[ch]});
                }
                group = std::move(res);
            } else {
                throw std::runtime_error("unknown logic: " + cond.logic);
            }
        }
        if (!group.empty()) {
            if (!hasResult) {
                result = std::move(group);
            } else {
                for (size_t ch = 0; ch < chunks; ch++)
                    result[ch] = QueryProcessing::Or(cc, {result[ch], group[ch]});
            }
        }

        patdiscover::saveCiphertexts(jp(d, kResultFile), result);
    TRY_END(err)
}

/* proxy (PRE) */

// PRE key: provider → researcher
int patdiscover_proxy_generate_ksk(const char *sk_researcher_dir,
                                   const char *sk_provider_dir,
                                   const char *ksk_path, char **err) {
    TRY_BEGIN
        std::string rdir(sk_researcher_dir);
        std::string pdir(sk_provider_dir);

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(pdir, patdiscover::kContextFile), cc);
        PrivateKey<DCRTPoly> skP;
        patdiscover::loadSecretKey(jp(pdir, patdiscover::kSecretKeyFile), skP);
        PublicKey<DCRTPoly> pkR;
        patdiscover::loadPublicKey(jp(rdir, patdiscover::kPublicKeyFile), pkR);

        auto rk = cc->ReKeyGen(skP, pkR);
        patdiscover::saveEvalKey(std::string(ksk_path), rk);
    TRY_END(err)
}

// re-encrypt, provider → researcher
int patdiscover_proxy_reencrypt(const char *ksk_path, const char *in_path,
                                const char *out_path, char **err) {
    TRY_BEGIN
        EvalKey<DCRTPoly> rk;
        patdiscover::loadEvalKey(std::string(ksk_path), rk);

        std::vector<Ciphertext<DCRTPoly>> src;
        patdiscover::loadCiphertexts(std::string(in_path), src);
        if (src.empty())
            throw std::runtime_error("patdiscover_proxy_reencrypt: empty input");

        auto cc = src[0]->GetCryptoContext();
        std::vector<Ciphertext<DCRTPoly>> dst;
        dst.reserve(src.size());
        for (const auto &ct : src)
            dst.push_back(cc->ReEncrypt(ct, rk));

        patdiscover::saveCiphertexts(std::string(out_path), dst);
    TRY_END(err)
}

// PRE key: researcher → provider
int patdiscover_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                      const char *sk_provider_dir,
                                      const char *ksk_path, char **err) {
    TRY_BEGIN
        std::string rdir(sk_researcher_dir);
        std::string pdir(sk_provider_dir);

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(rdir, patdiscover::kContextFile), cc);
        PrivateKey<DCRTPoly> skR;
        patdiscover::loadSecretKey(jp(rdir, patdiscover::kSecretKeyFile), skR);
        PublicKey<DCRTPoly> pkP;
        patdiscover::loadPublicKey(jp(pdir, patdiscover::kPublicKeyFile), pkP);

        auto rk = cc->ReKeyGen(skR, pkP);
        patdiscover::saveEvalKey(std::string(ksk_path), rk);
    TRY_END(err)
}

// re-encrypt literals, researcher → provider
int patdiscover_proxy_reencrypt_literals(const char *ksk_path, const char *in_path,
                                          const char *out_path, char **err) {
    TRY_BEGIN
        EvalKey<DCRTPoly> rk;
        patdiscover::loadEvalKey(std::string(ksk_path), rk);

        std::vector<Ciphertext<DCRTPoly>> src;
        patdiscover::loadCiphertexts(std::string(in_path), src);
        if (src.empty())
            throw std::runtime_error(
                "patdiscover_proxy_reencrypt_literals: empty input");

        auto cc = src[0]->GetCryptoContext();
        std::vector<Ciphertext<DCRTPoly>> dst;
        dst.reserve(src.size());
        for (const auto &ct : src)
            dst.push_back(cc->ReEncrypt(ct, rk));

        patdiscover::saveCiphertexts(std::string(out_path), dst);
    TRY_END(err)
}

/* encrypted literals */

// encrypt literals
// values arrive as a JSON array of doubles in the un-normalized domain; the
// provider applies the per-column (min, max) map at evaluation time
int patdiscover_encrypt_literals(const char *researcher_dir, const char *values_json,
                                  const char *out_path, char **err) {
    TRY_BEGIN
        std::string rdir(researcher_dir);
        std::vector<double> values =
            json::parse(std::string(values_json)).get<std::vector<double>>();

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(rdir, patdiscover::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        patdiscover::loadPublicKey(jp(rdir, patdiscover::kPublicKeyFile), pk);
        const size_t slots = cc->GetEncodingParams()->GetBatchSize();

        // literals are broadcast: one CT per value, all slots equal
        std::vector<Ciphertext<DCRTPoly>> cts;
        cts.reserve(values.size());
        for (double v : values)
            cts.push_back(encConst(cc, pk, slots, v));

        patdiscover::saveCiphertexts(std::string(out_path), cts);
    TRY_END(err)
}

// evaluate with encrypted literals
// literal CTs are already key-switched into the provider's domain and carry
// the un-normalized value, normalized on the fly before the comparator
int patdiscover_provider_evaluate_enc_literals(const char *dir,
                                               const char *instructions,
                                               const char *literals_path,
                                               char **err) {
    TRY_BEGIN
        initUpstreamOnce();

        std::string d(dir);

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(d, patdiscover::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        patdiscover::loadPublicKey(jp(d, patdiscover::kPublicKeyFile), pk);
        patdiscover::loadEvalMultKeys(jp(d, patdiscover::kEvalMultKeyFile), cc);

        std::vector<Ciphertext<DCRTPoly>> cts;
        patdiscover::loadCiphertexts(jp(d, kEncDataFile), cts);

        std::vector<ColumnSpec> cols;
        size_t rows = 0, slots = 0, chunks = 0;
        readMeta(d, cols, rows, slots, chunks);

        if (cts.size() != cols.size() * chunks)
            throw std::runtime_error("ciphertext count does not match columns×chunks");

        std::vector<Ciphertext<DCRTPoly>> lits;
        patdiscover::loadCiphertexts(std::string(literals_path), lits);

        auto conds = parseInstructions(std::string(instructions));
        if (conds.empty()) throw std::runtime_error("empty instructions");
        if (lits.size() < conds.size())
            throw std::runtime_error("not enough encrypted literals for query");

        // must use exactly normVal's affine map, (v-min)/(max-min)*0.8 + 0.2:
        // mapping the literal into [0, 1] instead sits every threshold below its
        // true position and GE then admits rows under the bound
        auto normalizeLit = [&](const Ciphertext<DCRTPoly> &lit,
                                const ColumnSpec &spec) {
            const double range = spec.maxVal - spec.minVal;
            if (range <= 0.0) return lit;
            std::vector<double> minVec(slots, spec.minVal);
            auto shifted = cc->EvalSub(lit, cc->MakeCKKSPackedPlaintext(minVec));
            auto scaled = cc->EvalMult(shifted, 0.8 / range);
            return cc->EvalAdd(scaled, 0.2);
        };

        // AND binds tighter than OR, see the plain-literal path
        std::vector<Ciphertext<DCRTPoly>> group, result;
        bool hasResult = false;
        for (size_t ci = 0; ci < conds.size(); ci++) {
            const auto &cond = conds[ci];
            if (cond.colIdx < 0 ||
                static_cast<size_t>(cond.colIdx) >= cols.size())
                throw std::runtime_error("column index out of range");

            const auto &spec = cols[static_cast<size_t>(cond.colIdx)];
            const auto normLit = normalizeLit(lits[ci], spec);

            std::vector<Ciphertext<DCRTPoly>> res(chunks);
            for (size_t ch = 0; ch < chunks; ch++) {
                const auto &colCt =
                    cts[static_cast<size_t>(cond.colIdx) * chunks + ch];
                res[ch] = evalConditionEncLit(cc, pk, colCt, normLit,
                                              cond.op, slots);
            }

            if (group.empty()) {
                group = std::move(res); continue;
            }
            if (cond.logic == "AND") {
                for (size_t ch = 0; ch < chunks; ch++)
                    group[ch] = QueryProcessing::And(cc, {group[ch], res[ch]});
            } else if (cond.logic == "OR") {
                if (!hasResult) {
                    result = std::move(group); hasResult = true;
                } else {
                    for (size_t ch = 0; ch < chunks; ch++)
                        result[ch] = QueryProcessing::Or(cc, {result[ch], group[ch]});
                }
                group = std::move(res);
            } else {
                throw std::runtime_error("unknown logic: " + cond.logic);
            }
        }
        if (!group.empty()) {
            if (!hasResult) {
                result = std::move(group);
            } else {
                for (size_t ch = 0; ch < chunks; ch++)
                    result[ch] = QueryProcessing::Or(cc, {result[ch], group[ch]});
            }
        }

        patdiscover::saveCiphertexts(jp(d, kResultFile), result);
    TRY_END(err)
}

/* consent blinding */

int patdiscover_setup_blinding(const char *provider_dir, const char *proxy_dir,
                               const char *patients_json, char **err) {
    TRY_BEGIN
        std::string pdir(provider_dir), qdir(proxy_dir);
        json patients = json::parse(std::string(patients_json));

        CryptoContext<DCRTPoly> cc;
        patdiscover::loadContext(jp(pdir, patdiscover::kContextFile), cc);
        PublicKey<DCRTPoly> pk;
        patdiscover::loadPublicKey(jp(pdir, patdiscover::kPublicKeyFile), pk);
        const size_t slots = cc->GetEncodingParams()->GetBatchSize();

        namespace fs = std::filesystem;
        const std::string blindDir = jp(pdir, "blindings");
        const std::string cancDir  = jp(qdir, "cancellations");
        fs::create_directories(blindDir);
        fs::create_directories(cancDir);

        // a mask CT is ~30 MB at RingDim 131072, so one per (patient, chunk)
        // on both sides is 300 GB at 10k patients: +r_p folds into one aggregate
        // CT per chunk, −r_p keeps only metadata and is rebuilt on demand

        // the cancellation store is shared federation-wide, so the stashed
        // public key is named per provider; one shared file left only the last
        // enroller's key and every other provider's rows stayed blinded
        const std::string pkName =
            "public_key_" + std::to_string(std::hash<std::string>{}(pdir)) + ".bin";
        {
            std::ifstream pkIn(jp(pdir, patdiscover::kPublicKeyFile),
                               std::ios::binary);
            std::ofstream pkOut(jp(cancDir, pkName),
                                std::ios::binary | std::ios::trunc);
            if (!pkIn.is_open() || !pkOut.is_open())
                throw std::runtime_error("cannot stash provider pk into proxy dir");
            pkOut << pkIn.rdbuf();
        }
        const std::string idxPath = jp(cancDir, "store.idx");
        std::ofstream idxF(idxPath, std::ios::app);
        if (!idxF.is_open()) throw std::runtime_error("cannot open store.idx");

        // stamp r_p into a plaintext slot vector and encrypt once per chunk:
        // N×EvalAdd at CT level SIGBUSed at 10k patients; store.idx is the
        // canonical record, so per-patient callers accumulate across calls
        std::map<int, std::vector<double>> blindAggPlain;
        {
            std::ifstream priorIdx(idxPath);
            std::string line;
            while (priorIdx.is_open() && std::getline(priorIdx, line)) {
                if (line.empty()) continue;
                json e = json::parse(line);
                const int chunk     = e.at("c").get<int>();
                const double rp_old = e.at("rp").get<double>();
                const auto slotIdx  = e.at("slots").get<std::vector<int>>();
                auto &agg = blindAggPlain[chunk];
                if (agg.empty()) agg.assign(slots, 0.0);
                for (int s : slotIdx)
                    if (s >= 0 && static_cast<size_t>(s) < slots) agg[s] = rp_old;
            }
        }
        auto loadAggForChunk = [&](int chunk) -> std::vector<double> & {
            auto it = blindAggPlain.find(chunk);
            if (it != blindAggPlain.end()) return it->second;
            blindAggPlain.emplace(chunk, std::vector<double>(slots, 0.0));
            return blindAggPlain.at(chunk);
        };

        for (auto it = patients.begin(); it != patients.end(); ++it) {
            const std::string pid = it.key();
            auto rows = it.value().get<std::vector<int>>();
            if (rows.empty()) continue;

            const double rp = sampleBlindingValue();   // +r_p in [2, 4)

            std::map<int, std::vector<int>> byChunk;
            for (int r : rows) {
                if (r < 0) continue;
                byChunk[r / static_cast<int>(slots)].push_back(
                    r % static_cast<int>(slots));
            }

            for (const auto &ce : byChunk) {
                const int chunk = ce.first;
                const auto &slotIdx = ce.second;

                // single encryption happens after the patient loop
                auto &agg = loadAggForChunk(chunk);
                for (int s : slotIdx)
                    if (s >= 0 && static_cast<size_t>(s) < slots)
                        agg[s] = rp;

                json e = {{"p", pid}, {"c", chunk},
                          {"rp", rp}, {"slots", slotIdx}, {"pk", pkName}};
                idxF << e.dump() << "\n";
            }
        }

        // one aggregate blinding CT per chunk
        for (const auto &kv : blindAggPlain) {
            const std::string path =
                jp(blindDir, "chunk_" + std::to_string(kv.first) + ".bin");
            std::ofstream bf(path, std::ios::binary | std::ios::trunc);
            if (!bf.is_open())
                throw std::runtime_error("cannot open blinding aggregate: " + path);
            auto ct = cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(kv.second));
            const std::string bytes = ctToBytes(ct);
            const uint64_t sz = bytes.size();
            bf.write(reinterpret_cast<const char *>(&sz), sizeof(sz));
            bf.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
    TRY_END(err)
}

int patdiscover_apply_blinding(const char *blindings_dir, const char *result_path,
                               const char *out_path, char **err) {
    TRY_BEGIN
        std::vector<Ciphertext<DCRTPoly>> result;
        patdiscover::loadCiphertexts(std::string(result_path), result);
        if (result.empty())
            throw std::runtime_error("patdiscover_apply_blinding: empty result");
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

        patdiscover::saveCiphertexts(std::string(out_path), result);
    TRY_END(err)
}

int patdiscover_apply_cancellation(const char *result_path, const char *cancel_dir,
                                   const char *consented_json, const char *out_path,
                                   char **err) {
    TRY_BEGIN
        auto consented = json::parse(std::string(consented_json))
                             .get<std::vector<std::string>>();

        std::vector<Ciphertext<DCRTPoly>> result;
        patdiscover::loadCiphertexts(std::string(result_path), result);
        if (result.empty())
            throw std::runtime_error("patdiscover_apply_cancellation: empty result");
        auto cc = result[0]->GetCryptoContext();

        if (!consented.empty()) {
            const std::string cdir(cancel_dir);

            // store.idx holds (patient, chunk, r_p, slots); the CT is built in
            // memory and added straight to the result, never written out
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
                    ent.pkName  = e.value("pk", std::string(patdiscover::kPublicKeyFile));
                    index[e.at("p").get<std::string>()].push_back(std::move(ent));
                }
            }

            // the pk stashed by setup_blinding lets the proxy rebuild masks
            // without holding any secret material
            std::map<std::string, PublicKey<DCRTPoly>> pkCache;
            auto pkFor = [&](const std::string &name) -> const PublicKey<DCRTPoly> & {
                auto it = pkCache.find(name);
                if (it == pkCache.end()) {
                    PublicKey<DCRTPoly> loaded;
                    patdiscover::loadPublicKey(jp(cdir, name), loaded);
                    it = pkCache.emplace(name, std::move(loaded)).first;
                }
                return it->second;
            };
            const size_t slots = cc->GetEncodingParams()->GetBatchSize();

            for (const auto &pid : consented) {
                auto it = index.find(pid);
                if (it == index.end()) continue;
                for (const auto &ent : it->second) {
                    if (ent.chunk < 0 ||
                        static_cast<size_t>(ent.chunk) >= result.size())
                        continue;
                    auto cancCT = buildMask(cc, pkFor(ent.pkName), slots, ent.slotIdx, -ent.rp);
                    result[static_cast<size_t>(ent.chunk)] =
                        cc->EvalAdd(result[static_cast<size_t>(ent.chunk)],
                                    cancCT);
                }
            }
        }

        patdiscover::saveCiphertexts(std::string(out_path), result);
    TRY_END(err)
}

}  // extern "C"
