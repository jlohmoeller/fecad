#include "serialize.hpp"
#include "fsync_util.hpp"
#include <fstream>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>

// every save_* fsyncs
// BeeGFS may drop the buffered write on close, leaving the next reader an
// ENOENT or zero-byte file; inner braces flush the ofstream before fsyncPath

void save_key(const std::string& filename,
               TFHEpp::SecretKey& sk1) {
    {
        std::ofstream os(filename, std::ios::binary);
        cereal::PortableBinaryOutputArchive ar(os);

        ar(sk1);
    }
    fecad::fsyncPath(filename);
}

void load_key(const std::string& filename,
               TFHEpp::SecretKey& sk1) {

    std::ifstream is(filename, std::ios::binary);
    cereal::PortableBinaryInputArchive ar(is);

    ar(sk1);
}

void save_eval_key(const std::string& filename,
               const TFHEpp::EvalKey& ek) {
    {
        std::ofstream os(filename, std::ios::binary);
        cereal::PortableBinaryOutputArchive ar(os);

        ar(ek);
    }
    fecad::fsyncPath(filename);
}

void load_eval_key(const std::string& filename,
               TFHEpp::EvalKey& ek) {

    std::ifstream is(filename, std::ios::binary);
    cereal::PortableBinaryInputArchive ar(is);

    ar(ek);
}

void save_ciphertexts(const std::string& filename, const std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>>& ciphertexts) {
    {
        std::ofstream os(filename, std::ios::binary);
        cereal::PortableBinaryOutputArchive ar(os);
        ar(ciphertexts);
    }
    fecad::fsyncPath(filename);
}

void load_ciphertexts(const std::string& filename, std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>>& ciphertexts) {
    std::ifstream is(filename, std::ios::binary);
    cereal::PortableBinaryInputArchive ar(is);
    ar(ciphertexts);
}

void save_ksk(const std::string& filename, const TFHEpp::KeySwitchingKey<TFHEpp::lvl11param>& ksk) {
    {
        std::ofstream os(filename, std::ios::binary);
        cereal::PortableBinaryOutputArchive ar(os);
        ar(ksk);
    }
    fecad::fsyncPath(filename);
}

void load_ksk(const std::string& filename, TFHEpp::KeySwitchingKey<TFHEpp::lvl11param>& ksk) {
    std::ifstream is(filename, std::ios::binary);
    cereal::PortableBinaryInputArchive ar(is);
    ar(ksk);
}
