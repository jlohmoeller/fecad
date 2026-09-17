#include "pdserver/grpc/TrustedAuthorityClient.hpp"

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

    void TrustedAuthorityClient::GetMultKey(AttributeType type, const CryptoContext &cc) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetMultKey(&context, req);

        PD_TRACE("Retrieving mult key chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve mult key: {}", status.error_message());
        }

        cc->DeserializeEvalMultKey(ss, lbcrypto::SerType::BINARY);
    }

    Ciphertext TrustedAuthorityClient::GetSchemeSwitchingKey(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetSchemeSwitchingKey(&context, req);

        PD_TRACE("Retrieveing scheme switching key chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve scheme switching key: {}", status.error_message());
        }

        Ciphertext result;
        lbcrypto::Serial::Deserialize(result, ss, lbcrypto::SerType::BINARY);

        return result;
    }

    void TrustedAuthorityClient::GetAutomorphismKey(AttributeType type, const CryptoContext &cc) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetAutomorphismKey(&context, req);

        PD_TRACE("Reading automorphism key chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve automorphism key: {}", status.error_message());
        }

        cc->DeserializeEvalAutomorphismKey(ss, lbcrypto::SerType::BINARY);
    }

    std::shared_ptr<lbcrypto::BinFHEContext> TrustedAuthorityClient::GetBinFheCryptoContext(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetBinFheCryptoContext(&context, req);

        PD_TRACE("Reading BinFHE crypto context chunks...");
        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);
        const auto status = reader->Finish();

        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve BinFHE crypto context: {}", status.error_message());
        }

        std::shared_ptr<lbcrypto::BinFHEContext> result;
        lbcrypto::Serial::Deserialize(result, ss, lbcrypto::SerType::BINARY);

        return result;
    }

    lbcrypto::RingGSWBTKey TrustedAuthorityClient::GetRefreshAndSwitchKey(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        std::stringstream bsKeyStream;
        std::stringstream ksKeyStream;

        {
            grpc::ClientContext context;

            proto::TaRequest req;
            req.set_type(static_cast<proto::AttributeType>(type));
            const auto reader = m_Stub->GetBinFheRefreshKey(&context, req);

            PD_TRACE("Reading refresh key chunks...");
            ReadChunkedToStream(reader.get(), bsKeyStream);
            const auto status = reader->Finish();

            if (!status.ok())
            {
                PD_ERROR("Failed to retrieve BinFHE refresh key: {}", status.error_message());
            }
        }


        {
            grpc::ClientContext context;

            proto::TaRequest req;
            req.set_type(static_cast<proto::AttributeType>(type));
            const auto reader = m_Stub->GetBinFheSwitchKey(&context, req);

            PD_TRACE("Reading switch key chunks...");
            ReadChunkedToStream(reader.get(), ksKeyStream);
            const auto status = reader->Finish();

            if (!status.ok())
            {
                PD_ERROR("Failed to retrieve BinFHE switch key: {}", status.error_message());
            }
        }

        lbcrypto::RingGSWBTKey result;
        lbcrypto::Serial::Deserialize(result.BSkey, bsKeyStream, lbcrypto::SerType::BINARY);
        lbcrypto::Serial::Deserialize(result.KSkey, ksKeyStream, lbcrypto::SerType::BINARY);

        return result;
    }

    void TrustedAuthorityClient::GetBinFheBootstrappingKey(AttributeType type, std::shared_ptr<lbcrypto::BinFHEContext> &ctx) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");

        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetBinFheBootstrappingKey(&context, req);

        lbcrypto::RingGSWBTKey element;

        bool initial = true;
        uint32_t lastIndex = 0;
        proto::KeyType lastKeyType = proto::KeyType::BS_KEY;

        PD_TRACE("Reading bootstrapping key chunks...");
        std::stringstream ss;
        proto::BootstrappingKeyChunked chunk;
        while (reader->Read(&chunk))
        {
            uint32_t index = chunk.index();
            proto::KeyType keyType = chunk.type();

            if (initial)
            {
                lastIndex = index;
                lastKeyType = keyType;
                initial = false;
            }

            if (index != lastIndex || keyType != lastKeyType)
            {
                if (lastKeyType == proto::KeyType::BS_KEY)
                {
                    lbcrypto::Serial::Deserialize(element.BSkey, ss, lbcrypto::SerType::BINARY);
                } else if (lastKeyType == proto::KeyType::KS_KEY)
                {
                    lbcrypto::Serial::Deserialize(element.KSkey, ss, lbcrypto::SerType::BINARY);
                    ctx->BTKeyMapLoadSingleElement(lastIndex, element);
                } else
                {
                    PD_ASSERT(false, "Invalid last key type");
                }

                lastIndex = index;
                lastKeyType = keyType;
            }

            ss << chunk.data();
        }

        if (lastKeyType == proto::KeyType::BS_KEY)
        {
            lbcrypto::Serial::Deserialize(element.BSkey, ss, lbcrypto::SerType::BINARY);
        } else if (lastKeyType == proto::KeyType::KS_KEY)
        {
            lbcrypto::Serial::Deserialize(element.KSkey, ss, lbcrypto::SerType::BINARY);
            ctx->BTKeyMapLoadSingleElement(lastIndex, element);
        } else
        {
            PD_ASSERT(false, "Invalid last key type");
        }

        const auto status = reader->Finish();
        if (!status.ok())
        {
            PD_ERROR("Failed to retrieve BinFHE bootstrapping key: {}", status.error_message());
        }
    }

#if PD_DEBUG
    lbcrypto::PrivateKey<lbcrypto::DCRTPoly> TrustedAuthorityClient::GetPrivateKey(AttributeType type) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Context Initialization");
        grpc::ClientContext context;

        proto::TaRequest req;
        req.set_type(static_cast<proto::AttributeType>(type));
        const auto reader = m_Stub->GetPrivateKey(&context, req);

        std::stringstream ss;
        ReadChunkedToStream(reader.get(), ss);

        if (const auto status = reader->Finish(); !status.ok())
        {
            PD_ERROR("Failed to retrieve private key: {}", status.error_message());
        }

        lbcrypto::PrivateKey<lbcrypto::DCRTPoly> result;
        lbcrypto::Serial::Deserialize(result, ss, lbcrypto::SerType::BINARY);

        return result;
    }
#endif
}
