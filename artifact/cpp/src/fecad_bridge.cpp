/* extern "C" wrappers over TFHEpp / HE3DB
   every entry point catches C++ exceptions and hands them back as a malloc'd
   string, so nothing unwinds across the CGo boundary */

#include "fecad_bridge.h"

#include <algorithm>
#include <cstdlib>   // strdup, free, getenv
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comparison/comparison.h"
#include "utils/utils.h"
#include "utils/serialize.hpp"
#include "key_switching.h"

using namespace HEDB;
using namespace TFHEpp;
using json = nlohmann::json;

using KSParam = TFHEpp::lvl11param;

/* helpers */

static char *dup_err(const std::string &msg) { return strdup(msg.c_str()); }

#define TRY_BEGIN try {
#define TRY_END(err_ptr)                                               \
    } catch (const std::exception &_e) {                               \
        if (err_ptr) *(err_ptr) = dup_err(_e.what());                  \
        return 1;                                                       \
    } catch (...) {                                                     \
        if (err_ptr) *(err_ptr) = dup_err("unknown C++ exception");    \
        return 1;                                                       \
    }                                                                   \
    return 0;

/* schema / metadata helpers */

static const char *kMetaFile = "he3db_meta.json";

static std::vector<std::string> loadSchemaColumns(const std::string &schemaPath) {
    if (schemaPath.empty()) return {};
    std::ifstream in(schemaPath);
    if (!in.is_open())
        throw std::runtime_error("cannot open schema: " + schemaPath);
    json s;
    in >> s;
    if (!s.contains("columns"))
        throw std::runtime_error("schema missing columns: " + schemaPath);
    struct Entry { int index; std::string name; };
    std::vector<Entry> entries;
    for (const auto &col : s["columns"])
        entries.push_back({col.at("index").get<int>(), col.at("name").get<std::string>()});
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.index < b.index;
    });
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto &e : entries) out.push_back(e.name);
    return out;
}

static void writeMeta(const std::string &dir, const std::vector<std::string> &columns, size_t rows) {
    json m;
    m["columns"] = columns;
    m["rows"] = rows;
    std::ofstream out(dir + "/" + kMetaFile);
    if (!out.is_open())
        throw std::runtime_error("cannot write " + std::string(kMetaFile));
    out << m.dump(2) << "\n";
}

static size_t readMetaNumColumns(const std::string &dir) {
    std::ifstream in(dir + "/" + kMetaFile);
    if (!in.is_open())
        throw std::runtime_error("cannot open " + std::string(kMetaFile) + " in " + dir);
    json m;
    in >> m;
    return m.at("columns").get<std::vector<std::string>>().size();
}

/* instruction parser (from he3db_wrapper.cpp) */

// anonymous namespace for internal linkage: each bridge .so defines its own
// Condition of a different size, and weak external ~vector<Condition> symbols
// get collapsed across them → destructor walks the wrong stride → SIGSEGV
namespace {

struct Condition {
    int col_idx;
    int target_val;
    std::string comp_op;
    std::string logic_op;
};

std::vector<Condition> parseInstructions(const std::string &s) {
    std::vector<Condition> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        std::stringstream inner(item);
        std::string seg;
        std::vector<std::string> parts;
        while (std::getline(inner, seg, ',')) parts.push_back(seg);
        if (parts.size() == 4)
            out.push_back({std::stoi(parts[0]), std::stoi(parts[1]),
                           parts[2], parts[3]});
    }
    return out;
}

} // namespace

/* extern "C" implementations */

extern "C" {

void fecad_free_string(char *s) { free(s); }

/* researcher */

int fecad_researcher_generate_key(const char *data_dir, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);
        TFHESecretKey sk;
        save_key(dir + "/secret_key.bin", sk);
    TRY_END(err)
}

int fecad_researcher_decrypt(const char *data_dir, const char *result_path,
                             const char *out_path, int rows, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);

        TFHESecretKey sk;
        load_key(dir + "/secret_key.bin", sk);

        std::vector<TLWELvl1> ctexts;
        load_ciphertexts(std::string(result_path), ctexts);

        std::vector<uint32_t> plain(ctexts.size());
        for (size_t i = 0; i < ctexts.size(); i++)
            plain[i] = TFHEpp::tlweSymDecrypt<Lvl1>(ctexts[i],
                                                     sk.key.get<Lvl1>());

        json j = plain;
        std::ofstream o(out_path);  // direct const char* — avoids most-vexing-parse
        if (!o.is_open())
            throw std::runtime_error("cannot open output: " +
                                     std::string(out_path));
        o << std::setw(4) << j << "\n";
    TRY_END(err)
}

/* provider */

int fecad_provider_generate_keys(const char *data_dir, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);
        TFHESecretKey sk;
        save_key(dir + "/secret_key.bin", sk);

        TFHEEvalKey ek;
        ek.emplacebkfft<Lvl01>(sk);
        ek.emplaceiksk<Lvl10>(sk);
        save_eval_key(dir + "/eval_key.bin", ek);
    TRY_END(err)
}

int fecad_provider_encrypt(const char *data_dir, const char *schema_path, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);
        std::string sp(schema_path ? schema_path : "");

        TFHESecretKey sk;
        load_key(dir + "/secret_key.bin", sk);

        json j;
        {
            std::ifstream f(dir + "/data.json");
            if (!f.is_open())
                throw std::runtime_error("cannot open " + dir + "/data.json");
            f >> j;
        }
        if (!j.is_array() || j.empty())
            throw std::runtime_error("data.json must be a non-empty array");

        std::vector<std::string> cols = loadSchemaColumns(sp);
        if (cols.empty()) {
            for (auto it = j[0].begin(); it != j[0].end(); ++it)
                cols.push_back(it.key());
            std::sort(cols.begin(), cols.end());
        }

        const size_t rows = j.size();
        constexpr uint32_t bits = 8;  // widest schema column domain (8-bit quantized labs)
        const uint32_t scale_bits =
            std::numeric_limits<Lvl1::T>::digits - bits - 1;
        const double scale = pow(2., scale_bits);

        std::vector<TLWELvl1> all;
        all.reserve(cols.size() * rows);

        for (const auto &col : cols) {
            for (size_t i = 0; i < rows; i++) {
                uint32_t v = j[i].contains(col) ? j[i].at(col).get<uint32_t>() : 0u;
                all.push_back(TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                    v, Lvl1::α, scale, sk.key.get<Lvl1>()));
            }
        }

        writeMeta(dir, cols, rows);
        save_ciphertexts(dir + "/encrypted_data.bin", all);
    TRY_END(err)
}

int fecad_provider_evaluate_dynamic(const char *data_dir,
                                    const char *instructions, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);

        TFHEEvalKey ek;
        load_eval_key(dir + "/eval_key.bin", ek);

        TFHESecretKey sk;
        load_key(dir + "/secret_key.bin", sk);

        std::vector<TLWELvl1> all;
        load_ciphertexts(dir + "/encrypted_data.bin", all);

        const size_t num_columns = readMetaNumColumns(dir);
        if (all.size() % num_columns != 0)
            throw std::runtime_error(
                "ciphertext count not divisible by num_columns");

        const size_t rows = all.size() / num_columns;
        std::vector<std::vector<TLWELvl1>> db(
            num_columns, std::vector<TLWELvl1>(rows));
        for (size_t c = 0; c < num_columns; c++)
            for (size_t r = 0; r < rows; r++)
                db[c][r] = all[c * rows + r];

        auto query = parseInstructions(std::string(instructions));

        constexpr uint32_t bits = 8;  // widest schema column domain (8-bit quantized labs)
        const uint32_t scale_bits =
            std::numeric_limits<Lvl1::T>::digits - bits - 1;
        const double scale = pow(2., scale_bits);

        std::map<int, TLWELvl1> lits;
        for (const auto &cond : query)
            if (lits.find(cond.target_val) == lits.end())
                lits[cond.target_val] = TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                    cond.target_val, Lvl1::α, scale, sk.key.get<Lvl1>());

        std::vector<TLWELvl1> results(rows);
        for (size_t i = 0; i < rows; i++) {
            TLWELvl1 row_result, group;
            bool first = true;
            bool has_result = false;
            for (const auto &cond : query) {
                if (cond.col_idx >= (int)num_columns)
                    throw std::runtime_error("column index out of bounds");
                TLWELvl1 left = db[cond.col_idx][i];
                TLWELvl1 right = lits.at(cond.target_val);
                TLWELvl1 match;
                if (cond.comp_op == "EQ")
                    equal<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LT")
                    less_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "GT")
                    greater_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LE")
                    less_than_equal<Lvl1>(left, right, match, bits, ek,
                                         LOGIC);
                else if (cond.comp_op == "GE")
                    greater_than_equal<Lvl1>(left, right, match, bits, ek,
                                            LOGIC);
                else
                    throw std::runtime_error("unknown op: " + cond.comp_op);

                // AND binds tighter than OR: conditions accumulate into an
                // AND-group, completed groups are ORed into the row result
                if (first) {
                    group = match;
                    first = false;
                } else if (cond.logic_op == "OR") {
                    if (has_result) {
                        TLWELvl1 tmp;
                        HomOR(tmp, row_result, group, ek, LOGIC);
                        row_result = tmp;
                    } else {
                        row_result = group;
                        has_result = true;
                    }
                    group = match;
                } else if (cond.logic_op == "AND") {
                    TLWELvl1 tmp;
                    HomAND(tmp, group, match, ek, LOGIC);
                    group = tmp;
                } else {
                    throw std::runtime_error("unknown logic op: " +
                                             cond.logic_op);
                }
            }
            if (has_result) {
                TLWELvl1 tmp;
                HomOR(tmp, row_result, group, ek, LOGIC);
                row_result = tmp;
            } else {
                row_result = group;
            }
            results[i] = row_result;
        }

        save_ciphertexts(dir + "/result_query.bin", results);
    TRY_END(err)
}

/* proxy */

// global statics: KSKs overflow the stack
static TFHEpp::KeySwitchingKey<KSParam> g_ksk;
static TFHEpp::KeySwitchingKey<KSParam> g_ksk_rl; // forward: researcher→provider

int fecad_proxy_generate_ksk(const char *sk_researcher,
                              const char *sk_location, const char *ksk_path,
                              char **err) {
    TRY_BEGIN
        TFHESecretKey skA, skB;
        load_key(std::string(sk_researcher) + "/secret_key.bin", skA);
        load_key(std::string(sk_location) + "/secret_key.bin", skB);
        TFHEEksxt::cross_ikskgen<KSParam>(g_ksk, skB, skA);
        save_ksk(std::string(ksk_path), g_ksk);
    TRY_END(err)
}

int fecad_proxy_aggregate(const char *ksk_path, const char *in_file,
                           const char *out_file, char **err) {
    TRY_BEGIN
        load_ksk(std::string(ksk_path), g_ksk);

        std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>> src;
        load_ciphertexts(std::string(in_file), src);

        std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>> dst(src.size());
        for (size_t i = 0; i < src.size(); i++)
            IdentityKeySwitch<KSParam>(dst[i], src[i], g_ksk);

        save_ciphertexts(std::string(out_file), dst);
    TRY_END(err)
}

/* forward KSK (researcher → provider) */

int fecad_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                  const char *sk_provider_dir,
                                  const char *ksk_path, char **err) {
    TRY_BEGIN
        TFHESecretKey skR, skP;
        load_key(std::string(sk_researcher_dir) + "/secret_key.bin", skR);
        load_key(std::string(sk_provider_dir) + "/secret_key.bin", skP);
        // domain=researcher, target=provider → IdentityKeySwitch converts R→P
        TFHEEksxt::cross_ikskgen<KSParam>(g_ksk_rl, skR, skP);
        save_ksk(std::string(ksk_path), g_ksk_rl);
    TRY_END(err)
}

int fecad_proxy_keyswitch_literals(const char *ksk_path, const char *in_path,
                                    const char *out_path, char **err) {
    TRY_BEGIN
        load_ksk(std::string(ksk_path), g_ksk_rl);
        std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>> src;
        load_ciphertexts(std::string(in_path), src);
        std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>> dst(src.size());
        for (size_t i = 0; i < src.size(); i++)
            IdentityKeySwitch<KSParam>(dst[i], src[i], g_ksk_rl);
        save_ciphertexts(std::string(out_path), dst);
    TRY_END(err)
}

/* encrypted literals */

int fecad_encrypt_literals(const char *researcher_dir, const char *values_json,
                            const char *out_path, char **err) {
    TRY_BEGIN
        std::vector<int64_t> values =
            json::parse(std::string(values_json)).get<std::vector<int64_t>>();

        TFHESecretKey sk;
        load_key(std::string(researcher_dir) + "/secret_key.bin", sk);

        constexpr uint32_t bits = 8;  // widest schema column domain (8-bit quantized labs)
        const uint32_t scale_bits =
            std::numeric_limits<Lvl1::T>::digits - bits - 1;
        const double scale = pow(2., scale_bits);

        std::vector<TLWELvl1> cts;
        cts.reserve(values.size());
        for (int64_t v : values)
            cts.push_back(TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                static_cast<int32_t>(v), Lvl1::α, scale, sk.key.get<Lvl1>()));

        save_ciphertexts(std::string(out_path), cts);
    TRY_END(err)
}

int fecad_provider_evaluate_enc_literals(const char *data_dir,
                                          const char *instructions,
                                          const char *literals_path, char **err) {
    TRY_BEGIN
        std::string dir(data_dir);

        TFHEEvalKey ek;
        load_eval_key(dir + "/eval_key.bin", ek);

        std::vector<TLWELvl1> all;
        load_ciphertexts(dir + "/encrypted_data.bin", all);

        const size_t num_columns = readMetaNumColumns(dir);
        if (num_columns == 0 || all.size() % num_columns != 0)
            throw std::runtime_error(
                "ciphertext count not divisible by num_columns");

        const size_t rows = all.size() / num_columns;
        std::vector<std::vector<TLWELvl1>> db(
            num_columns, std::vector<TLWELvl1>(rows));
        for (size_t c = 0; c < num_columns; c++)
            for (size_t r = 0; r < rows; r++)
                db[c][r] = all[c * rows + r];

        auto query = parseInstructions(std::string(instructions));

        std::vector<TLWELvl1> lits;
        load_ciphertexts(std::string(literals_path), lits);
        if (lits.size() < query.size())
            throw std::runtime_error("not enough encrypted literals for query");

        constexpr uint32_t bits = 8;  // widest schema column domain (8-bit quantized labs)

        std::vector<TLWELvl1> results(rows);
        for (size_t i = 0; i < rows; i++) {
            TLWELvl1 row_result, group;
            bool first = true;
            bool has_result = false;
            for (size_t ci = 0; ci < query.size(); ci++) {
                const auto &cond = query[ci];
                if (cond.col_idx < 0 ||
                    static_cast<size_t>(cond.col_idx) >= num_columns)
                    throw std::runtime_error("column index out of bounds");

                TLWELvl1 left = db[cond.col_idx][i];
                TLWELvl1 right = lits[ci];
                TLWELvl1 match;

                if (cond.comp_op == "EQ")
                    equal<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LT")
                    less_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "GT")
                    greater_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LE")
                    less_than_equal<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "GE")
                    greater_than_equal<Lvl1>(left, right, match, bits, ek,
                                             LOGIC);
                else
                    throw std::runtime_error("unknown op: " + cond.comp_op);

                // AND binds tighter than OR: conditions accumulate into an
                // AND-group, completed groups are ORed into the row result
                if (first) {
                    group = match;
                    first = false;
                } else if (cond.logic_op == "OR") {
                    if (has_result) {
                        TLWELvl1 tmp;
                        HomOR(tmp, row_result, group, ek, LOGIC);
                        row_result = tmp;
                    } else {
                        row_result = group;
                        has_result = true;
                    }
                    group = match;
                } else if (cond.logic_op == "AND") {
                    TLWELvl1 tmp;
                    HomAND(tmp, group, match, ek, LOGIC);
                    group = tmp;
                } else {
                    throw std::runtime_error("unknown logic op: " +
                                             cond.logic_op);
                }
            }
            if (has_result) {
                TLWELvl1 tmp;
                HomOR(tmp, row_result, group, ek, LOGIC);
                row_result = tmp;
            } else {
                row_result = group;
            }
            results[i] = row_result;
        }

        save_ciphertexts(dir + "/result_query.bin", results);
    TRY_END(err)
}

/* patient blinding */

int fecad_setup_blinding(const char *provider_dir, const char *proxy_dir,
                         const char *patients_json, char **err) {
    TRY_BEGIN
        std::string pdir(provider_dir);
        std::string qdir(proxy_dir);
        json patients = json::parse(std::string(patients_json));

        TFHESecretKey sk;
        load_key(pdir + "/secret_key.bin", sk);

        constexpr uint32_t bits = 8;  // widest schema column domain (8-bit quantized labs)
        const uint32_t scale_bits =
            std::numeric_limits<Lvl1::T>::digits - bits - 1;
        const double scale = pow(2., scale_bits);

        std::filesystem::create_directories(pdir + "/blindings");
        std::filesystem::create_directories(qdir + "/cancellations");

        // read the row→patient map once, extend in memory, write back once:
        // per-patient calls would rewrite patients.json O(n^2) times
        const std::string pjson = pdir + "/patients.json";
        std::vector<int> row_to_patient;
        std::vector<std::string> patient_ids;
        {
            std::ifstream f(pjson);
            if (f.is_open()) {
                json j; f >> j;
                if (j.contains("row_to_patient"))
                    row_to_patient =
                        j["row_to_patient"].get<std::vector<int>>();
                if (j.contains("patient_ids"))
                    patient_ids =
                        j["patient_ids"].get<std::vector<std::string>>();
            }
        }

        for (auto it = patients.begin(); it != patients.end(); ++it) {
            const std::string pid = it.key();
            std::vector<int> rowIndices = it.value().get<std::vector<int>>();
            if (rowIndices.empty()) continue;

            uint32_t rp = 0;
            {
                std::ifstream urnd("/dev/urandom", std::ios::binary);
                if (!urnd.read(reinterpret_cast<char *>(&rp), sizeof(rp)))
                    throw std::runtime_error("cannot read /dev/urandom");
            }

            TLWELvl1 pos_ct = TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                rp, Lvl1::α, scale, sk.key.get<Lvl1>());
            save_ciphertexts(pdir + "/blindings/" + pid + ".bin", {pos_ct});

            // Enc(-rp) = component-wise negation
            TLWELvl1 neg_ct;
            for (size_t i = 0; i <= Lvl1::n; i++)
                neg_ct[i] =
                    static_cast<Lvl1::T>(-static_cast<int64_t>(pos_ct[i]));
            save_ciphertexts(qdir + "/cancellations/" + pid + ".bin",
                             {neg_ct});

            int patIdx = -1;
            for (int i = 0; i < static_cast<int>(patient_ids.size()); i++)
                if (patient_ids[i] == pid) { patIdx = i; break; }
            if (patIdx < 0) {
                patIdx = static_cast<int>(patient_ids.size());
                patient_ids.push_back(pid);
            }

            int maxRow = 0;
            for (int r : rowIndices) if (r > maxRow) maxRow = r;
            while (static_cast<int>(row_to_patient.size()) <= maxRow)
                row_to_patient.push_back(-1);
            for (int r : rowIndices)
                row_to_patient[r] = patIdx;
        }

        json pj;
        pj["row_to_patient"] = row_to_patient;
        pj["patient_ids"] = patient_ids;
        std::ofstream of(pjson);
        if (!of.is_open())
            throw std::runtime_error("cannot write patients.json in " + pdir);
        of << pj.dump(2) << "\n";
    TRY_END(err)
}

int fecad_apply_blinding(const char *working_dir, const char *result_path,
                          const char *out_path, char **err) {
    TRY_BEGIN
        std::string wdir(working_dir);

        json patients;
        {
            std::ifstream f(wdir + "/patients.json");
            if (!f.is_open())
                throw std::runtime_error("cannot open patients.json in " + wdir);
            f >> patients;
        }
        const auto row_to_patient =
            patients.at("row_to_patient").get<std::vector<int>>();
        const auto patient_ids =
            patients.at("patient_ids").get<std::vector<std::string>>();

        std::vector<TLWELvl1> result;
        load_ciphertexts(std::string(result_path), result);

        std::map<int, TLWELvl1> cache;
        for (size_t i = 0; i < result.size(); i++) {
            if (i >= row_to_patient.size()) break;
            const int pat_idx = row_to_patient[i];
            if (pat_idx < 0 ||
                static_cast<size_t>(pat_idx) >= patient_ids.size())
                continue;

            if (cache.find(pat_idx) == cache.end()) {
                std::vector<TLWELvl1> bcts;
                load_ciphertexts(
                    wdir + "/blindings/" + patient_ids[pat_idx] + ".bin",
                    bcts);
                if (bcts.empty())
                    throw std::runtime_error("empty blinding for " +
                                             patient_ids[pat_idx]);
                cache[pat_idx] = bcts[0];
            }
            const TLWELvl1 &blind = cache.at(pat_idx);
            for (size_t j = 0; j <= Lvl1::n; j++)
                result[i][j] += blind[j];
        }

        save_ciphertexts(std::string(out_path), result);
    TRY_END(err)
}

int fecad_apply_cancellation(const char *result_path,
                              const char *cancellations_dir,
                              const char *consented_json, const char *out_path,
                              char **err) {
    TRY_BEGIN
        std::string cdir(cancellations_dir);

        // row_to_patient.json: string array (row index → patient ID)
        std::vector<std::string> row_to_patient;
        {
            std::ifstream f(cdir + "/row_to_patient.json");
            if (f.is_open()) {
                json j; f >> j;
                row_to_patient = j.get<std::vector<std::string>>();
            }
        }

        std::set<std::string> consented;
        {
            json j = json::parse(std::string(consented_json));
            for (const auto &s : j)
                consented.insert(s.get<std::string>());
        }

        std::vector<TLWELvl1> result;
        load_ciphertexts(std::string(result_path), result);

        std::map<std::string, TLWELvl1> cache;
        for (size_t i = 0;
             i < result.size() && i < row_to_patient.size(); i++) {
            const std::string &pid = row_to_patient[i];
            if (pid.empty() || consented.find(pid) == consented.end())
                continue;

            if (cache.find(pid) == cache.end()) {
                std::vector<TLWELvl1> cts;
                load_ciphertexts(cdir + "/" + pid + ".bin", cts);
                if (cts.empty())
                    throw std::runtime_error("empty cancellation for " + pid);
                cache[pid] = cts[0];
            }
            const TLWELvl1 &cancel = cache.at(pid);
            for (size_t j = 0; j <= Lvl1::n; j++)
                result[i][j] += cancel[j];
        }

        save_ciphertexts(std::string(out_path), result);
    TRY_END(err)
}

} /* extern "C" */
