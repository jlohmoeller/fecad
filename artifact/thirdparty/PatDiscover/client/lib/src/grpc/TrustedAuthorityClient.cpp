#include "pdclient/grpc/TrustedAuthorityClient.hpp"

#include <pdshared/grpc/Chunking.hpp>
#include <pdshared/measure/CommunicationCost.hpp>

namespace pat_disc {
    TrustedAuthorityClient::TrustedAuthorityClient(const std::shared_ptr<grpc::Channel> &channel)
        : m_Stub(proto::TrustedAuthority::NewStub(channel))
    {
    }

    CryptoContext TrustedAuthorityClient::GetCryptoContext(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetCryptoContext(&context, req);

        PD_TRACE("Retrieving crypto context chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve crypto context: {}", status.error_message());
        }

        CryptoContext result;
        lbcrypto::Serial::Deserialize(result, ss, lbcrypto::SerType::BINARY);

        return result;
    }

    lbcrypto::PublicKey<lbcrypto::DCRTPoly> TrustedAuthorityClient::GetPublicKey(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetPublicKey(&context, req);

        PD_TRACE("Retrieving public key chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve public key: {}", status.error_message());
        }

        lbcrypto::PublicKey<lbcrypto::DCRTPoly> pk;
        lbcrypto::Serial::Deserialize(pk, ss, lbcrypto::SerType::BINARY);

        return pk;
    }

    lbcrypto::EvalKey<lbcrypto::DCRTPoly> TrustedAuthorityClient::GetKeySwitchingKey(AttributeType type, const lbcrypto::PublicKey<lbcrypto::DCRTPoly> &pk) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;
        const auto readWrite = m_Stub->GetKeySwitchingKey(&context);

        std::stringstream pkStream;
        lbcrypto::Serial::Serialize(pk, pkStream, lbcrypto::SerType::BINARY);

        proto::KeySwitchingKeyRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        req.set_public_key(pkStream.str());
        WriteChunkedFromMessage(readWrite.get(), req);
        readWrite->WritesDone();

        PD_TRACE("Retrieving key switching key chunks...");
        std::stringstream ksStream;
        ReadChunkedToStream(readWrite.get(), ksStream);
        const auto status = readWrite->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve key switching key: {}", status.error_message());
        }

        lbcrypto::EvalKey<lbcrypto::DCRTPoly> result;
        lbcrypto::Serial::Deserialize(result, ksStream, lbcrypto::SerType::BINARY);

        return result;
    }

#if PD_EVAL
    void TrustedAuthorityClient::Kill() const
    {
        grpc::ClientContext context;
        const google::protobuf::Empty req;
        const auto reader = m_Stub->Kill(&context, req);
        reader->Finish();
    }
#endif
}
