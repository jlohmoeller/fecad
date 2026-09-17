#ifndef ENGORGIO_IO_HPP
#define ENGORGIO_IO_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "openfhe.h"

#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#include "scheme/ckksrns/ckksrns-ser.h"

namespace engorgio {

constexpr const char *kContextFile = "crypto_context.bin";
constexpr const char *kPublicKeyFile = "public_key.bin";
constexpr const char *kSecretKeyFile = "secret_key.bin";
constexpr const char *kEvalMultKeyFile = "eval_key.bin";

lbcrypto::CryptoContext<lbcrypto::DCRTPoly> createContext();

void saveContext(const std::string &path, const lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc);
void loadContext(const std::string &path, lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc);

void savePublicKey(const std::string &path, const lbcrypto::PublicKey<lbcrypto::DCRTPoly> &pk);
void loadPublicKey(const std::string &path, lbcrypto::PublicKey<lbcrypto::DCRTPoly> &pk);

void saveSecretKey(const std::string &path, const lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &sk);
void loadSecretKey(const std::string &path, lbcrypto::PrivateKey<lbcrypto::DCRTPoly> &sk);

void saveEvalMultKeys(const std::string &path, const lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc);
void loadEvalMultKeys(const std::string &path, const lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc);

void saveEvalKey(const std::string &path, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ek);
void loadEvalKey(const std::string &path, lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ek);

void saveCiphertexts(const std::string &path,
                     const std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> &cts);
void loadCiphertexts(const std::string &path,
                     std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> &cts);

}  // namespace engorgio

#endif  // ENGORGIO_IO_HPP
