#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

#include "comparison/comparison.h"
#include "utils/utils.h"
#include "utils/serialize.hpp"
#include "key_switching.h"

using namespace std;
using namespace TFHEpp;
using namespace HEDB;

using KSParam = TFHEpp::lvl11param;

// global static: KSK overflows the stack
static TFHEpp::KeySwitchingKey<KSParam> kskL1_to_R;

void generate_key(std::string sk_researcher, std::string sk_location, const std::string& out_path) {
    std::cout << "Generating switching keys..." << std::endl;

    TFHESecretKey skA;
    load_key(sk_researcher + "/secret_key.bin", skA);

    TFHESecretKey skB;
    load_key(sk_location + "/secret_key.bin", skB);

    TFHEEksxt::cross_ikskgen<KSParam>(kskL1_to_R, skB, skA);

    save_ksk(out_path, kskL1_to_R);

    std::cout << "Key generated and saved to " << out_path << std::endl;
}

void aggregate(const std::string& key_path, const std::string& in_file, 
               const std::string& out_file) {
    
    std::cout << "Loading switching key..." << std::endl;

    load_ksk(key_path, kskL1_to_R);

    std::cout << "Loading encrypted evaluations..." << std::endl;
    vector<TFHEpp::TLWE<TFHEpp::lvl1param>> resultLocation;

    load_ciphertexts(in_file, resultLocation);

    std::cout << "Processing results of locations (Key Switching)..." << std::endl;
    size_t rows_per_loc = resultLocation.size();
    
    vector<TFHEpp::TLWE<TFHEpp::lvl1param>> resultQueryFinal(rows_per_loc);
    std::cout << "Processing " << rows_per_loc << " rows." << std::endl;

    for (size_t i = 0; i < rows_per_loc; i++) {
        IdentityKeySwitch<KSParam>(resultQueryFinal[i], resultLocation[i], kskL1_to_R);
    }

    std::cout << "Saving aggregated result to " << out_file << std::endl;
    save_ciphertexts(out_file, resultQueryFinal);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode> [args...]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  generate_key <sk_researcher> <sk_location> <out_path>" << std::endl;
        std::cerr << "  aggregate <keys_dir> <in_file> <out_file>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "generate_key") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " generate_key <sk_researcher> <sk_location> <out_path>" << std::endl;
            return 1;
        }
        std::string sk_researcher = argv[2];
        std::string sk_location = argv[3];
        std::string out_path = argv[4];
        generate_key(sk_researcher, sk_location, out_path);
    } else if (mode == "aggregate") {
        if (argc < 5) {
            std::cerr << "Usage: " << argv[0] << " aggregate <ksk_path> <in_file> <out_file>" << std::endl;
            return 1;
        }
        std::string ksk_path = argv[2];
        std::string in_file = argv[3];
        std::string out_file = argv[4];
        aggregate(ksk_path, in_file, out_file);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }

    return 0;
}