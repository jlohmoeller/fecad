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

        [[nodiscard]] lbcrypto::EvalKey<lbcrypto::DCRTPoly> GetKeySwitchingKey(AttributeType type, const lbcrypto::PublicKey<lbcrypto::DCRTPoly> &pk) const;

#if PD_EVAL
        void Kill() const;
#endif

    private:
        std::unique_ptr<proto::TrustedAuthority::Stub> m_Stub;
    };
}


#endif //TRUSTEDAUTHORITYCLIENT_HPP
