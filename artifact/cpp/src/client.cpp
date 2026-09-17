#include <iostream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <string>

#include "utils/utils.h"
#include "utils/serialize.hpp"

using namespace HEDB;
using namespace std;
using namespace TFHEpp;
using json = nlohmann::json;

int generate_key(std::string working_dir)
{
    TFHESecretKey sk;
    save_key(working_dir + "/secret_key.bin", sk);

    return 0;
}

int decrypt(std::string working_dir, std::string encrypted_data_path, std::string out_path, size_t rows)
{
    TFHESecretKey sk;
    try {
        load_key(working_dir + "/secret_key.bin", sk);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load key from " << working_dir + "/secret_key.bin" << ": " << e.what() << std::endl;
        return 1;
    }

    std::vector<TLWELvl1> resultQueryEval;
    try {
        load_ciphertexts(encrypted_data_path, resultQueryEval);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load encrypted data from " << encrypted_data_path << ": " << e.what() << std::endl;
        return 1;
    }

    vector<uint32_t> resultQueryOne(resultQueryEval.size());

    std::cout << "Decrypting " << resultQueryEval.size() << " rows..." << std::endl;

    for (size_t i = 0; i < resultQueryEval.size(); i++)
    {
        resultQueryOne[i] = TFHEpp::tlweSymDecrypt<Lvl1>(
            resultQueryEval[i],
            sk.key.get<Lvl1>());
    }

    cout << "Decrypted result:             ";
    for (auto a : resultQueryOne)
        cout << a << " "; 
    cout << endl;

    json j_out = resultQueryOne;
    std::ofstream o(out_path);
    if (o.is_open()) {
        o << std::setw(4) << j_out << std::endl;
        std::cout << "Decrypted results saved to " << out_path << std::endl;
    } else {
        std::cerr << "Failed to save results to " << out_path << std::endl;
    }
    
    return 0;
}


int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  generate_key <working_dir>" << std::endl;
        std::cerr << "  decrypt <working_dir> <encrypted_data_path> <out_path> <rows>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "generate_key") {
        return generate_key(argv[2]);
    } else if (mode == "decrypt") {
        return decrypt(argv[2], argv[3], argv[4], std::stoi(argv[5]));
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
} 