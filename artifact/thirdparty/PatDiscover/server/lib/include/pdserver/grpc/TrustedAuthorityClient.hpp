#ifndef TRUSTEDAUTHORITYCLIENT_HPP
#define TRUSTEDAUTHORITYCLIENT_HPP

#include <grpc++/channel.h>
#include <pdproto/services.grpc.pb.h>

namespace pat_disc {
    class TrustedAuthorityClient
    {
    public:
        explicit TrustedAuthorityClient(const std::shared_ptr<grpc::Channel> &channel);

        [[nodiscard]] CryptoContext GetCryptoContext(AttributeType type) const;

        [[nodiscard]] lbcrypto::PublicKey<lbcrypto::DCRTPoly> GetPublicKey(AttributeType type) const;

        void GetMultKey(AttributeType type, const CryptoContext &cc) const;

        [[nodiscard]] Ciphertext GetSchemeSwitchingKey(AttributeType type) const;

        void GetAutomorphismKey(AttributeType type, const CryptoContext &cc) const;

        [[nodiscard]] std::shared_ptr<lbcrypto::BinFHEContext> GetBinFheCryptoContext(AttributeType type) const;

        [[nodiscard]] lbcrypto::RingGSWBTKey GetRefreshAndSwitchKey(AttributeType type) const;

        void GetBinFheBootstrappingKey(AttributeType type, std::shared_ptr<lbcrypto::BinFHEContext> &ctx) const;

#if PD_DEBUG
        lbcrypto::PrivateKey<lbcrypto::DCRTPoly> GetPrivateKey(AttributeType type) const;
#endif

    private:
        std::unique_ptr<proto::TrustedAuthority::Stub> m_Stub;
    };
}


#endif //TRUSTEDAUTHORITYCLIENT_HPP
