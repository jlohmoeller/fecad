#include "pdclient/entity/Client.hpp"

#include <grpc++/create_channel.h>
#include <pdclient/grpc/TrustedAuthorityClient.hpp>
#include <pdshared/grpc/CostMetricInterceptor.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Serialization.hpp>
#include <pdshared/util/Utilities.hpp>

#include "pdclient/config/CommandLineParameters.hpp"
#include "pdclient/data/PatientDataLoader.hpp"
#include "pdclient/data/QueryBuilder.hpp"
#include "pdclient/processing/ClientQueryProcessing.hpp"

#include "pdclient/data/QueryParser.hpp"

namespace pat_disc {
    Client::Client(const std::string &providerName, const std::shared_ptr<grpc::Channel> &channel)
        : m_Client(channel), m_ProviderName(providerName)
    {
    }

    Client::Client(std::string &&providerName, const std::shared_ptr<grpc::Channel> &channel)
        : m_Client(channel), m_ProviderName(std::move(providerName))
    {
    }

    void Client::Initialize()
    {
        if (!client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface> > creators;
            creators.push_back(std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>(new ClientCostMetricInterceptorFactory()));

            auto credOptions = grpc::SslCredentialsOptions();
            credOptions.pem_root_certs = ReadFileContents("data/certificates/ca-cert.pem");
            credOptions.pem_private_key = ReadFileContents("data/certificates/client-key.pem");
            credOptions.pem_cert_chain = ReadFileContents("data/certificates/client-cert.pem");
            const auto channelCredentials = SslCredentials(credOptions);

            // Create gRPC channel to TA
            const grpc::ChannelArguments args;
            const auto channel = CreateCustomChannelWithInterceptors("localhost:50001", channelCredentials, args, std::move(creators));
            const TrustedAuthorityClient taClient(channel);

            // Retrieve keys and context
            RetrieveStrategyInformation(m_InformationBoolean, AttributeType::BOOLEAN, taClient);
            RetrieveStrategyInformation(m_InformationEnumPrecise, AttributeType::ENUM_PRECISE, taClient);
            RetrieveStrategyInformation(m_InformationEnumApprox, AttributeType::ENUM_APPROX, taClient);
            RetrieveStrategyInformation(m_InformationContinuousPrecise, AttributeType::CONTINUOUS_PRECISE, taClient);
            RetrieveStrategyInformation(m_InformationContinuousApprox, AttributeType::CONTINUOUS_APPROX, taClient);
            RetrieveStrategyInformation(m_InformationDistancePrecise, AttributeType::DISTANCE_PRECISE, taClient);
            RetrieveStrategyInformation(m_InformationDistanceApprox, AttributeType::DISTANCE_APPROX, taClient);

#if PD_EVAL
            if (client::CommandLineParameters::GetInstance().IsShutdownServers())
            {
                taClient.Kill(); // Kill trusted authority
            }
#endif
        }
    }

    uint64_t Client::AddPatientData(const std::string &filePath)
    {
        Timer::GetInstance().Switch("Data Loading");
        const auto &[ids, data] = PatientDataLoader::LoadData(filePath);
        m_Client.StartDataUpload();

        {
            PD_TRACE("Uploading patient identifiers...");

            constexpr int32_t kIdBatchSize = 100000;
            proto::PatientDataUpload protoPatientData;
            proto::IdentifierData *idData = protoPatientData.mutable_id_data();
            idData->set_provider(m_ProviderName);

            for (size_t i = 0; i < ids.size(); i++)
            {
                idData->add_ids(ids[i]);

                if ((i + 1) % kIdBatchSize == 0)
                {
                    Timer::GetInstance().Switch("Data Upload");
                    m_Client.UploadPatientData(protoPatientData);
                    Timer::GetInstance().Switch("Data Loading");

                    idData->clear_ids();
                }
            }

            if (ids.size() % kIdBatchSize != 0)
            {
                Timer::GetInstance().Switch("Data Upload");
                m_Client.UploadPatientData(protoPatientData);
                Timer::GetInstance().Switch("Data Loading");
            }
        }

        {
            if (client::CommandLineParameters::GetInstance().IsPlaintext())
            {
                for (const auto &attr: std::views::keys(MatchableAttribute::GetAttributes()))
                {
                    PD_TRACE("Uploading plaintexts for attribute {}...", attr);

                    const auto &vec = data.at(attr);
                    for (const auto &entry: vec)
                    {
                        proto::PatientDataUpload protoPatientData;
                        proto::AttributeData *attributeData = protoPatientData.mutable_attribute_data();
                        attributeData->set_attribute(attr);

                        switch (MatchableAttribute::GetAttributeType(attr))
                        {
                            case AttributeType::BOOLEAN:
                            case AttributeType::ENUM_PRECISE:
                            case AttributeType::ENUM_APPROX:
                            case AttributeType::CONTINUOUS_PRECISE:
                            case AttributeType::CONTINUOUS_APPROX:
                                attributeData->set_ciphertext(CreateIntegerPlaintext(entry));
                                break;
                            case AttributeType::DISTANCE_PRECISE:
                            case AttributeType::DISTANCE_APPROX:
                                attributeData->set_ciphertext(CreateDistancePlaintext(entry));
                                break;
                            case AttributeType::TYPE_COUNT:
                                PD_ASSERT(false, "Use of invalid attribute type");
                                break;
                        }

                        Timer::GetInstance().Switch("Data Upload");
                        m_Client.UploadPatientData(protoPatientData);
                        Timer::GetInstance().Switch("Data Loading");
                    }
                }
            } else
            {
                for (const auto &attr: std::views::keys(MatchableAttribute::GetAttributes()))
                {
                    PD_TRACE("Uploading ciphertexts for attribute {}...", attr);

                    const auto &vec = data.at(attr);
                    for (const auto &entry: vec)
                    {
                        proto::PatientDataUpload protoPatientData;
                        proto::AttributeData *attributeData = protoPatientData.mutable_attribute_data();
                        attributeData->set_attribute(attr);

                        switch (MatchableAttribute::GetAttributeType(attr))
                        {
                            case AttributeType::BOOLEAN:
                                attributeData->set_ciphertext(CreateCiphertextBoolean(entry));
                                break;
                            case AttributeType::ENUM_PRECISE:
                                attributeData->set_ciphertext(CreateCiphertextEnumPrecise(entry));
                                break;
                            case AttributeType::ENUM_APPROX:
                                attributeData->set_ciphertext(CreateCiphertextEnumApprox(entry));
                                break;
                            case AttributeType::CONTINUOUS_PRECISE:
                                attributeData->set_ciphertext(CreateCiphertextContinuousPrecise(entry));
                                break;
                            case AttributeType::CONTINUOUS_APPROX:
                                attributeData->set_ciphertext(CreateCiphertextContinuousApprox(entry));
                                break;
                            case AttributeType::DISTANCE_PRECISE:
                                attributeData->set_ciphertext(CreateCiphertextDistancePrecise(entry));
                                break;
                            case AttributeType::DISTANCE_APPROX:
                                attributeData->set_ciphertext(CreateCiphertextDistanceApprox(entry));
                                break;
                            case AttributeType::TYPE_COUNT:
                                PD_ASSERT(false, "Use of invalid attribute type");
                                break;
                        }

                        Timer::GetInstance().Switch("Data Upload");
                        m_Client.UploadPatientData(protoPatientData);
                        Timer::GetInstance().Switch("Data Loading");
                    }
                }
            }
        }

        m_Client.EndDataUpload();
        Timer::GetInstance().Switch("Idle");

        // Return number of patients uploaded
        return ids.size();
    }

    void Client::ExecuteQuery(const std::string &filePath) const
    {
        PD_TRACE("Starting query loading from file: {}", filePath);

        QueryBuilder qb;
        qb.Parse(filePath);

        RunQuery(qb);
    }

    void Client::ExecuteSqlQuery(const std::string &query) const
    {
        QueryParser qp;
        const QueryBuilder qb = qp.ParseQueryString(query);
        qb.Print();

        //RunQuery(qb);
    }

    void Client::Shutdown() const
    {
#if PD_EVAL
        if (client::CommandLineParameters::GetInstance().IsShutdownServers())
        {
            m_Client.Kill();
        }
#endif
    }

    void Client::RetrieveStrategyInformation(ClientContextInfo &strategyInformation, const AttributeType &type, const TrustedAuthorityClient &client)
    {
        if (!MatchableAttribute::GetAttributeCountForType(type))
            return;

        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalMultKeys();
        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalAutomorphismKeys();
        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalSumKeys();
        lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::ReleaseAllContexts();
        strategyInformation.cc = client.GetCryptoContext(type);

        // We need to retrieve the full crypto context to be able to perform key generation
        strategyInformation.cc = lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::GetFullContextByDeserializedContext(strategyInformation.cc);
        strategyInformation.keyPair = strategyInformation.cc->KeyGen(); // Generate client key pair
        strategyInformation.taPubKey = client.GetPublicKey(type);
        strategyInformation.preKey = client.GetKeySwitchingKey(type, strategyInformation.keyPair.publicKey);
    }


    std::string Client::CreateCiphertextBoolean(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<int64_t> data;
        data.reserve(vec.size());

        std::ranges::transform(vec, std::back_inserter(data), [](auto &item) -> int64_t
        {
            return std::get<int64_t>(item);
        });

        Timer::GetInstance().Switch("Data Encryption");
        const auto pt = m_InformationBoolean.cc->MakePackedPlaintext(data);
        const auto ct = m_InformationBoolean.cc->Encrypt(pt, m_InformationBoolean.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        return SerializeToString(ct);
    }

    std::string Client::CreateCiphertextEnumPrecise(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<int64_t> data;
        data.reserve(vec.size());

        std::ranges::transform(vec, std::back_inserter(data), [](auto &item) -> int64_t
        {
            return std::get<int64_t>(item);
        });

        Timer::GetInstance().Switch("Data Encryption");
        const auto pt = m_InformationEnumPrecise.cc->MakePackedPlaintext(data);
        const auto ct = m_InformationEnumPrecise.cc->Encrypt(pt, m_InformationEnumPrecise.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        return SerializeToString(ct);
    }

    std::string Client::CreateCiphertextEnumApprox(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<double> data;
        data.reserve(vec.size());

        std::ranges::transform(vec, std::back_inserter(data), [](auto &item) -> double
        {
            return std::get<double>(item);
        });

        Timer::GetInstance().Switch("Data Encryption");
        const auto pt = m_InformationEnumApprox.cc->MakeCKKSPackedPlaintext(data);
        const auto ct = m_InformationEnumApprox.cc->Encrypt(pt, m_InformationEnumApprox.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        return SerializeToString(ct);
    }

    std::string Client::CreateCiphertextContinuousPrecise(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<int64_t> data;
        data.reserve(vec.size());

        std::ranges::transform(vec, std::back_inserter(data), [](auto &item) -> double
        {
            return std::get<int64_t>(item);
        });

        Timer::GetInstance().Switch("Data Encryption");
        const auto pt = m_InformationContinuousPrecise.cc->MakePackedPlaintext(data);
        const auto ct = m_InformationContinuousPrecise.cc->Encrypt(pt, m_InformationContinuousPrecise.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        return SerializeToString(ct);
    }

    std::string Client::CreateCiphertextContinuousApprox(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<double> data;
        data.reserve(vec.size());

        std::ranges::transform(vec, std::back_inserter(data), [](auto &item) -> double
        {
            return std::get<double>(item);
        });

        Timer::GetInstance().Switch("Data Encryption");
        const auto pt = m_InformationContinuousApprox.cc->MakeCKKSPackedPlaintext(data);
        const auto ct = m_InformationContinuousApprox.cc->Encrypt(pt, m_InformationContinuousApprox.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        return SerializeToString(ct);
    }

    std::string Client::CreateCiphertextDistancePrecise(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<int64_t> dataX;
        std::vector<int64_t> dataY;
        std::vector<int64_t> dataZ;
        dataX.reserve(vec.size());
        dataY.reserve(vec.size());
        dataZ.reserve(vec.size());

        for (const auto &item: vec)
        {
            const auto &[x, y, z] = std::get<Position<int64_t>>(item);
            dataX.push_back(x);
            dataY.push_back(y);
            dataZ.push_back(z);
        }

        Timer::GetInstance().Switch("Data Encryption");
        const lbcrypto::Plaintext ptX = m_InformationDistancePrecise.cc->MakePackedPlaintext(dataX);
        const lbcrypto::Plaintext ptY = m_InformationDistancePrecise.cc->MakePackedPlaintext(dataY);
        const lbcrypto::Plaintext ptZ = m_InformationDistancePrecise.cc->MakePackedPlaintext(dataZ);

        const auto ctX = m_InformationDistancePrecise.cc->Encrypt(ptX, m_InformationDistancePrecise.taPubKey);
        const auto ctY = m_InformationDistancePrecise.cc->Encrypt(ptY, m_InformationDistancePrecise.taPubKey);
        const auto ctZ = m_InformationDistancePrecise.cc->Encrypt(ptZ, m_InformationDistancePrecise.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        proto::Position pos;
        pos.set_ciphertext_x(SerializeToString(ctX));
        pos.set_ciphertext_y(SerializeToString(ctY));
        pos.set_ciphertext_z(SerializeToString(ctZ));

        std::string res;
        pos.SerializeToString(&res);

        return res;
    }

    std::string Client::CreateCiphertextDistanceApprox(const std::vector<VectorAttributeData> &vec) const
    {
        std::vector<double> dataX;
        std::vector<double> dataY;
        std::vector<double> dataZ;
        dataX.reserve(vec.size());
        dataY.reserve(vec.size());
        dataZ.reserve(vec.size());

        for (const auto &item: vec)
        {
            const auto &[x, y, z] = std::get<Position<double>>(item);
            dataX.push_back(x);
            dataY.push_back(y);
            dataZ.push_back(z);
        }

        Timer::GetInstance().Switch("Data Encryption");
        const auto ptX = m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(dataX);
        const auto ptY = m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(dataY);
        const auto ptZ = m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(dataZ);

        const auto ctX = m_InformationDistanceApprox.cc->Encrypt(ptX, m_InformationDistanceApprox.taPubKey);
        const auto ctY = m_InformationDistanceApprox.cc->Encrypt(ptY, m_InformationDistanceApprox.taPubKey);
        const auto ctZ = m_InformationDistanceApprox.cc->Encrypt(ptZ, m_InformationDistanceApprox.taPubKey);
        Timer::GetInstance().Switch("Data Loading");

        proto::Position pos;
        pos.set_ciphertext_x(SerializeToString(ctX));
        pos.set_ciphertext_y(SerializeToString(ctY));
        pos.set_ciphertext_z(SerializeToString(ctZ));

        std::string res;
        pos.SerializeToString(&res);

        return res;
    }

    std::string Client::CreateIntegerPlaintext(const std::vector<VectorAttributeData> &vec)
    {
        proto::IntegerPlaintext pt;
        for (const auto &entry: vec)
        {
            pt.add_values(std::get<int64_t>(entry));
        }

        return pt.SerializeAsString();
    }

    std::string Client::CreateDistancePlaintext(const std::vector<VectorAttributeData> &vec)
    {
        proto::Position pos;
        proto::IntegerPlaintext *xPt = pos.mutable_plain_x();
        proto::IntegerPlaintext *yPt = pos.mutable_plain_y();
        proto::IntegerPlaintext *zPt = pos.mutable_plain_z();

        for (const auto &item: vec)
        {
            const auto &[x, y, z] = std::get<Position<int64_t>>(item);
            xPt->add_values(x);
            yPt->add_values(y);
            zPt->add_values(z);
        }

        std::string res;
        pos.SerializeToString(&res);

        return res;
    }

    void Client::RunQuery(QueryBuilder &qb) const
    {
        proto::QueryRequest req;
        qb.Pack(*this, req.mutable_data());

        proto::KeySwitchingKeys *keys = req.mutable_keys();

        // Only send re-encryption keys needed for query
        std::set<AttributeType> usedAttributeTypes;
        qb.GetUsedAttributeTypes(usedAttributeTypes);
        for (const auto &type: usedAttributeTypes)
        {
            lbcrypto::EvalKey<lbcrypto::DCRTPoly> preKey;
            switch (type)
            {
                case AttributeType::BOOLEAN:
                    preKey = m_InformationBoolean.preKey;
                    break;
                case AttributeType::ENUM_PRECISE:
                    preKey = m_InformationEnumPrecise.preKey;
                    break;
                case AttributeType::ENUM_APPROX:
                    preKey = m_InformationEnumApprox.preKey;
                    break;
                case AttributeType::CONTINUOUS_PRECISE:
                    preKey = m_InformationContinuousPrecise.preKey;
                    break;
                case AttributeType::CONTINUOUS_APPROX:
                    preKey = m_InformationContinuousApprox.preKey;
                    break;
                case AttributeType::DISTANCE_PRECISE:
                    preKey = m_InformationDistancePrecise.preKey;
                    break;
                case AttributeType::DISTANCE_APPROX:
                    preKey = m_InformationDistanceApprox.preKey;
                    break;
                case AttributeType::TYPE_COUNT:
                    PD_ASSERT(false, "Use of invalid attribute type");
                    break;
            }

            SerializeKey(type, preKey, keys);
        }

        PD_TRACE("Sending query");

        m_Client.SendQuery(req, this);
    }

    void Client::SerializeKey(AttributeType type, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey, proto::KeySwitchingKeys *keys)
    {
        if (!MatchableAttribute::GetAttributeCountForType(type))
            return;

        proto::SingleKey *k = keys->add_keys();

        std::ostringstream ss;
        lbcrypto::Serial::Serialize(ksKey, ss, lbcrypto::SerType::BINARY);
        k->set_attribute_type(static_cast<proto::AttributeType>(type));
        k->set_key(ss.str());
    }
}
