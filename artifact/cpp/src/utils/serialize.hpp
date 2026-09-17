#ifndef SERIALIZE_HPP
#define SERIALIZE_HPP

#include <tfhe++.hpp>
#include <string>

void save_key(const std::string& filename, TFHEpp::SecretKey& sk);
void load_key(const std::string& filename, TFHEpp::SecretKey& sk);

void save_eval_key(const std::string& filename, const TFHEpp::EvalKey& ek);
void load_eval_key(const std::string& filename, TFHEpp::EvalKey& ek);

void save_ciphertexts(const std::string& filename, const std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>>& ciphertexts);
void load_ciphertexts(const std::string& filename, std::vector<TFHEpp::TLWE<TFHEpp::lvl1param>>& ciphertexts);

void save_ksk(const std::string& filename, const TFHEpp::KeySwitchingKey<TFHEpp::lvl11param>& ksk);
void load_ksk(const std::string& filename, TFHEpp::KeySwitchingKey<TFHEpp::lvl11param>& ksk);

#endif