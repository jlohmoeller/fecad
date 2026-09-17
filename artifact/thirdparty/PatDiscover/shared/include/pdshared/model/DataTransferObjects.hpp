#ifndef SERVER_DATATRANSFEROBJECTS_HPP
#define SERVER_DATATRANSFEROBJECTS_HPP

namespace pat_disc {
    using IntegerEncoding = std::shared_ptr<std::vector<int64_t> >;
    using RealEncoding = std::shared_ptr<std::vector<double> >;

    using CryptoContext = lbcrypto::CryptoContext<lbcrypto::DCRTPoly>;
    using Ciphertext = lbcrypto::Ciphertext<lbcrypto::DCRTPoly>;
}

#endif //SERVER_DATATRANSFEROBJECTS_HPP
