#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comparison/comparison.h"
#include "utils/utils.h"
#include "utils/serialize.hpp"

using namespace HEDB;
using namespace TFHEpp;
using json = nlohmann::json;

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
        throw std::runtime_error("cannot write he3db_meta.json in " + dir);
    out << m.dump(2) << "\n";
}

static size_t readMetaNumColumns(const std::string &dir) {
    std::ifstream in(dir + "/" + kMetaFile);
    if (!in.is_open())
        throw std::runtime_error("cannot open he3db_meta.json in " + dir);
    json m;
    in >> m;
    return m.at("columns").get<std::vector<std::string>>().size();
}

struct Condition {
    int col_idx;
    int target_val;
    std::string comp_op; // EQ, LT, GT, LE, GE
    std::string logic_op; // AND, OR, NONE 
};

std::vector<Condition> parseInstructions(std::string instructions) {
    std::vector<Condition> conditions;
    std::stringstream ss(instructions);
    std::string item;

    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        std::stringstream ss_inner(item);
        std::string segment;
        std::vector<std::string> parts;
        while (std::getline(ss_inner, segment, ',')) {
            parts.push_back(segment);
        }
        if (parts.size() == 4) {
            conditions.push_back({
                std::stoi(parts[0]), 
                std::stoi(parts[1]), 
                parts[2], 
                parts[3]
            });
        }
    }
    return conditions;
}

int generate_keys(std::string secret_key_path)
{
    TFHESecretKey skA;
    save_key(secret_key_path + "/secret_key.bin", skA);

    std::cout << "Generating eval key..." << std::endl;
    TFHEEvalKey ek;
    ek.emplacebkfft<Lvl01>(skA);
    ek.emplaceiksk<Lvl10>(skA);

    save_eval_key(secret_key_path + "/eval_key.bin", ek);

    return 0;
}

int encrypt_data(std::string working_dir, std::string schema_path)
{
    TFHESecretKey skA;
    try {
        load_key(working_dir + "/secret_key.bin", skA);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load key: " << e.what() << std::endl;
        return 1;
    }

    json j;
    {
        std::ifstream f(working_dir + "/data.json");
        if (!f.is_open()) { std::cerr << "Cannot open data.json" << std::endl; return 1; }
        f >> j;
    }
    if (!j.is_array() || j.empty()) {
        std::cerr << "data.json must be a non-empty array" << std::endl;
        return 1;
    }

    std::vector<std::string> cols = loadSchemaColumns(schema_path);
    if (cols.empty()) {
        for (auto it = j[0].begin(); it != j[0].end(); ++it)
            cols.push_back(it.key());
        std::sort(cols.begin(), cols.end());
    }

    const size_t rows = j.size();
    std::cout << "Detected " << rows << " rows, " << cols.size() << " columns." << std::endl;

    uint32_t bits = 6;
    uint32_t scale_bits = std::numeric_limits<Lvl1::T>::digits - bits - 1;
    double scale = pow(2., scale_bits);

    std::vector<TLWELvl1> allCiphers;
    allCiphers.reserve(cols.size() * rows);

    for (const auto& col : cols) {
        for (size_t i = 0; i < rows; i++) {
            uint32_t v = j[i].contains(col) ? j[i].at(col).get<uint32_t>() : 0u;
            allCiphers.push_back(TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                v, Lvl1::α, scale, skA.key.get<Lvl1>()));
        }
    }

    writeMeta(working_dir, cols, rows);
    std::cout << "Saving encrypted data to " << working_dir << std::endl;
    save_ciphertexts(working_dir + "/encrypted_data.bin", allCiphers);
    return 0;
}

int evaluate_query(std::string working_dir)
{
    TFHEEvalKey ek;
    try {
        load_eval_key(working_dir + "/eval_key.bin", ek);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load eval key from " << working_dir << ": " << e.what() << std::endl;
        return 1;
    }

    uint32_t bits = 6;
    uint32_t scale_bits = std::numeric_limits<Lvl1::T>::digits - bits - 1;

    TFHESecretKey skA;
    try {
        load_key(working_dir + "/secret_key.bin", skA);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load secret key from " << working_dir << ": " << e.what() << std::endl;
        return 1;
    }

    std::vector<TLWELvl1> allCiphers;
    try {
        load_ciphertexts(working_dir + "/encrypted_data.bin", allCiphers);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ciphertexts from " << working_dir << ": " << e.what() << std::endl;
        return 1;
    }

    if (allCiphers.size() % 2 != 0) {
        std::cerr << "Error: Loaded un-even number of ciphertexts, expected 2 columns." << std::endl;
        return 1;
    }
    
    size_t rows = allCiphers.size() / 2;
    std::cout << "Detected " << rows << " rows from encrypted file." << std::endl;

    std::vector<TLWELvl1> whoGradeCiphers(allCiphers.begin(), allCiphers.begin() + rows);
    std::vector<TLWELvl1> mgmtPromoterMethylationCiphers(allCiphers.begin() + rows, allCiphers.end());

    Lvl1::T plain0 = 0;
    TLWELvl1 cipher0 = TFHEpp::tlweSymInt32Encrypt<Lvl1>(
        plain0,
        Lvl1::α,
        pow(2., scale_bits),
        skA.key.get<Lvl1>());

    std::vector<TLWELvl1> whoGradeEval(rows), mgmtPromoterMethylationEval(rows), resultQueryOneEval(rows);

    for (size_t i = 0; i < rows; i++)
    {
        equal<Lvl1>(whoGradeCiphers[i], cipher0, whoGradeEval[i], bits, ek, LOGIC);
        equal<Lvl1>(mgmtPromoterMethylationCiphers[i], cipher0, mgmtPromoterMethylationEval[i], bits, ek, LOGIC);
        HomAND(resultQueryOneEval[i], whoGradeEval[i], mgmtPromoterMethylationEval[i], ek, LOGIC);
    }

    std::cout << "Saving encrypted results..." << std::endl;
    save_ciphertexts(working_dir + "/result_query.bin", resultQueryOneEval);
    std::cout << "Results saved to " << working_dir << std::endl;

    return 0;
}

int evaluate_dynamic_query(std::string working_dir, std::string instructions)
{
    TFHEEvalKey ek;
    try {
        load_eval_key(working_dir + "/eval_key.bin", ek);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load eval key: " << e.what() << std::endl;
        return 1;
    }

    TFHESecretKey skA;
    try {
        load_key(working_dir + "/secret_key.bin", skA);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load secret key: " << e.what() << std::endl;
        return 1;
    }

    std::vector<TLWELvl1> allCiphers;
    try {
        load_ciphertexts(working_dir + "/encrypted_data.bin", allCiphers);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ciphertexts: " << e.what() << std::endl;
        return 1;
    }


    size_t num_columns = 0;
    try {
        num_columns = readMetaNumColumns(working_dir);
    } catch (const std::exception& e) {
        std::cerr << "Failed to read metadata: " << e.what() << std::endl;
        return 1;
    }
    if (num_columns == 0 || allCiphers.size() % num_columns != 0) {
        std::cerr << "Error: Ciphertext total size not divisible by num_columns ("
                  << num_columns << ")." << std::endl;
        return 1;
    }

    
    size_t rows = allCiphers.size() / num_columns;
    std::vector<std::vector<TLWELvl1>> database(num_columns, std::vector<TLWELvl1>(rows));
    
    for(size_t col = 0; col < num_columns; ++col) {
        for(size_t row = 0; row < rows; ++row) {
            database[col][row] = allCiphers[col * rows + row];
        }
    }

    std::vector<Condition> query = parseInstructions(instructions);
    
    uint32_t bits = 6; 
    uint32_t scale_bits = std::numeric_limits<Lvl1::T>::digits - bits - 1;
    double scale = pow(2., scale_bits);

    std::map<int, TLWELvl1> encrypted_literals;
    for (const Condition& c : query) {
        if (encrypted_literals.find(c.target_val) == encrypted_literals.end()) {
            encrypted_literals[c.target_val] = TFHEpp::tlweSymInt32Encrypt<Lvl1>(
                c.target_val, Lvl1::α, scale, skA.key.get<Lvl1>());
        }
    }

    std::cout << "4 - Start Evaluation" << std::endl;

    std::vector<TLWELvl1> results(rows);
    for (size_t i = 0; i < rows; i++) {
        // AND binds tighter than OR: fold AND-matches into group_acc, on OR
        // flush group_acc into result_acc and start a new group, flush the
        // last group at end of row
        TLWELvl1 group_acc, result_acc;
        bool has_group = false, has_result = false;

        for (size_t c_idx = 0; c_idx < query.size(); ++c_idx) {
            const Condition& cond = query[c_idx];

            if (cond.col_idx < 0 || static_cast<size_t>(cond.col_idx) >= num_columns) {
                std::cerr << "ERROR: Column index " << cond.col_idx << " out of bounds (max " << num_columns-1 << ")" << std::endl;
                return 1;
            }

            TLWELvl1 match;
            TLWELvl1 left = database[cond.col_idx][i];

            if (encrypted_literals.find(cond.target_val) == encrypted_literals.end()) {
                std::cerr << "ERROR: Literal " << cond.target_val << " not encrypted!" << std::endl;
                return 1;
            }
            TLWELvl1 right = encrypted_literals[cond.target_val];

            try {
                if (cond.comp_op == "EQ")
                    equal<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LT")
                    less_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "GT")
                    greater_than<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "LE")
                    less_than_equal<Lvl1>(left, right, match, bits, ek, LOGIC);
                else if (cond.comp_op == "GE")
                    greater_than_equal<Lvl1>(left, right, match, bits, ek, LOGIC);
            } catch (const std::exception& e) {
                std::cerr << "CRASH in Comparison: " << e.what() << " | Val: " << cond.target_val << " | Bits: " << bits << std::endl;
                return 1;
            }

            if (!has_group) {
                group_acc = match;
                has_group = true;
            } else if (cond.logic_op == "AND") {
                TLWELvl1 temp;
                HomAND(temp, group_acc, match, ek, LOGIC);
                group_acc = temp;
            } else if (cond.logic_op == "OR") {
                if (!has_result) {
                    result_acc = group_acc;
                    has_result = true;
                } else {
                    TLWELvl1 temp;
                    HomOR(temp, result_acc, group_acc, ek, LOGIC);
                    result_acc = temp;
                }
                group_acc = match;
            }
        }
        if (has_group) {
            if (!has_result) {
                result_acc = group_acc;
            } else {
                TLWELvl1 temp;
                HomOR(temp, result_acc, group_acc, ek, LOGIC);
                result_acc = temp;
            }
        }
        results[i] = result_acc;

        if (i % 10 == 0) std::cout << "Processed row " << i << std::endl;
    }

    std::cout << "5 - Finished Evaluation" << std::endl;

    std::cout << "Saving encrypted results..." << std::endl;
    save_ciphertexts(working_dir + "/result_query.bin", results);
    return 0;
}

// write one patient's masks
// blindings/ must already exist, working_dir ends with '/'
int append_blinding(std::string working_dir, std::string patient_id,
                    std::string rp_str, std::string cancel_path)
{
    TFHESecretKey sk;
    try {
        load_key(working_dir + "secret_key.bin", sk);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load key: " << e.what() << std::endl;
        return 1;
    }

    const uint32_t rp32 = static_cast<uint32_t>(std::stoull(rp_str));
    const uint32_t bits = 6;
    const uint32_t scale_bits = std::numeric_limits<Lvl1::T>::digits - bits - 1;
    const double scale = pow(2., scale_bits);

    TLWELvl1 pos_ct = TFHEpp::tlweSymInt32Encrypt<Lvl1>(rp32, Lvl1::α, scale, sk.key.get<Lvl1>());
    save_ciphertexts(working_dir + "blindings/" + patient_id + ".bin", {pos_ct});

    // Enc(-r_p) = component-wise negation of Enc(+r_p)
    TLWELvl1 neg_ct;
    for (size_t i = 0; i <= Lvl1::n; i++)
        neg_ct[i] = static_cast<Lvl1::T>(-static_cast<int64_t>(pos_ct[i]));
    save_ciphertexts(cancel_path, {neg_ct});

    return 0;
}

// add per-row blinding CTs
// working_dir ends with '/' and holds patients.json plus blindings/{id}.bin
int apply_blinding(std::string working_dir, std::string result_path, std::string out_path)
{
    json patients;
    {
        std::ifstream f(working_dir + "patients.json");
        if (!f.is_open()) {
            std::cerr << "Cannot open patients.json" << std::endl;
            return 1;
        }
        f >> patients;
    }
    const auto row_to_patient = patients.at("row_to_patient").get<std::vector<int>>();
    const auto patient_ids    = patients.at("patient_ids").get<std::vector<std::string>>();

    std::vector<TLWELvl1> result;
    try {
        load_ciphertexts(result_path, result);
    } catch (const std::exception& e) {
        std::cerr << "Cannot load result: " << e.what() << std::endl;
        return 1;
    }

    std::map<int, TLWELvl1> blinding_cache;

    for (size_t i = 0; i < result.size(); i++) {
        if (i >= row_to_patient.size()) break;
        const int pat_idx = row_to_patient[i];
        if (pat_idx < 0 || static_cast<size_t>(pat_idx) >= patient_ids.size()) continue;

        if (blinding_cache.find(pat_idx) == blinding_cache.end()) {
            std::string bpath = working_dir + "blindings/" + patient_ids[pat_idx] + ".bin";
            std::vector<TLWELvl1> bcts;
            try {
                load_ciphertexts(bpath, bcts);
            } catch (const std::exception& e) {
                std::cerr << "Cannot load blinding for " << patient_ids[pat_idx]
                          << ": " << e.what() << std::endl;
                return 1;
            }
            if (bcts.empty()) {
                std::cerr << "Empty blinding file for " << patient_ids[pat_idx] << std::endl;
                return 1;
            }
            blinding_cache[pat_idx] = bcts[0];
        }

        const TLWELvl1& blind = blinding_cache.at(pat_idx);
        for (size_t j = 0; j <= Lvl1::n; j++)
            result[i][j] += blind[j];
    }

    save_ciphertexts(out_path, result);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "generate") {
        std::string working_dir = (argc < 3) ? "." : argv[2];
        generate_keys(working_dir);
    } else if (mode == "encrypt") {
        std::string working_dir = (argc < 3) ? "." : argv[2];
        std::string schema_path = (argc >= 4) ? argv[3] : "";
        encrypt_data(working_dir, schema_path);
    } else if (mode == "evaluate") {
        std::string working_dir = (argc < 3) ? "." : argv[2];
        evaluate_query(working_dir);
    } else if (mode == "evaluate_dynamic") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " evaluate_dynamic <working_dir> <instructions>" << std::endl;
            return 1;
        }
        evaluate_dynamic_query(argv[2], argv[3]);
    } else if (mode == "append_blinding") {
        if (argc < 6) {
            std::cerr << "Usage: " << argv[0] << " append_blinding <working_dir/> <patient_id> <rp> <cancel_path>" << std::endl;
            return 1;
        }
        return append_blinding(argv[2], argv[3], argv[4], argv[5]);
    } else if (mode == "apply_blinding") {
        if (argc < 5) {
            std::cerr << "Usage: " << argv[0] << " apply_blinding <working_dir/> <result_path> <out_path>" << std::endl;
            return 1;
        }
        return apply_blinding(argv[2], argv[3], argv[4]);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }

    return 0;
}