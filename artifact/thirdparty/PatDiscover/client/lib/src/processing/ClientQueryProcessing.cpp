#include "pdclient/processing/ClientQueryProcessing.hpp"

#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Serialization.hpp>

#include "pdclient/config/CommandLineParameters.hpp"
#include "pdclient/entity/Client.hpp"

namespace pat_disc {
    ClientQueryProcessing::ClientQueryProcessing(const Client *client,
                                                 const std::unique_ptr<grpc::ClientReaderWriter<proto::ChunkedBytePayload, proto::QueryResponse> > &readWrite): m_Client(
        client)
    {
        std::vector<PatientResultInfo> resultInfos;

        proto::QueryResponse response;
        while (readWrite->Read(&response))
        {
            Timer::GetInstance().Switch("Query Execution");
            if (response.has_result())
            {
                OnResultAvailable(response.result());
            } else if (response.has_patient_info())
            {
                const proto::PatientToCiphertexts &patientInfo = response.patient_info();
                std::map<AttributeType, std::pair<int64_t, int64_t> > infoMap;

                for (const auto &ctInfo: patientInfo.ciphertext_ids())
                {
                    auto type = static_cast<AttributeType>(ctInfo.attribute_type());
                    infoMap.insert({type, {ctInfo.ct_id(), ctInfo.ct_index()}});
                }

                resultInfos.emplace_back(patientInfo.patient_id(), patientInfo.provider(), std::move(infoMap));
            }
            Timer::GetInstance().Switch("Idle");
        }

        Timer::GetInstance().Switch("Result Serialization");
        WriteResultsToJson(resultInfos);
        Timer::GetInstance().Switch("Idle");
    }

    void ClientQueryProcessing::OnResultAvailable(const proto::QueryResult &res)
    {
        const auto type = static_cast<AttributeType>(res.attribute_type());
        const int64_t ctId = res.ct_id();

        switch (type)
        {
            case AttributeType::BOOLEAN:
                ProcessBooleanResults(ctId, res.result());
                break;
            case AttributeType::ENUM_PRECISE:
                ProcessEnumPreciseResults(ctId, res.result());
                break;
            case AttributeType::ENUM_APPROX:
                ProcessEnumApproxResults(ctId, res.result());
                break;
            case AttributeType::CONTINUOUS_PRECISE:
                ProcessContinuousPreciseResults(ctId, res.result());
                break;
            case AttributeType::CONTINUOUS_APPROX:
                ProcessContinuousApproxResults(ctId, res.result());
                break;
            case AttributeType::DISTANCE_PRECISE:
                ProcessDistancePreciseResults(ctId, res.result());
                break;
            case AttributeType::DISTANCE_APPROX:
                ProcessDistanceApproxResults(ctId, res.result());
                break;
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Use of invalid attribute type")
                break;
        }
    }

    void ClientQueryProcessing::ProcessBooleanResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<int64_t> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(value);
            }
        } else
        {
            std::istringstream ss(result);
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct;
            lbcrypto::Serial::Deserialize(ct, ss, lbcrypto::SerType::BINARY);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationBoolean.cc->Decrypt(ct, m_Client->m_InformationBoolean.keyPair.secretKey, &pt);
            *values = pt->GetPackedValue();
        }

        m_BooleanResults.insert({ctId, values});
    }

    void ClientQueryProcessing::ProcessEnumPreciseResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<int64_t> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(value);
            }
        } else
        {
            std::istringstream ss(result);
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct;
            lbcrypto::Serial::Deserialize(ct, ss, lbcrypto::SerType::BINARY);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationEnumPrecise.cc->Decrypt(ct, m_Client->m_InformationEnumPrecise.keyPair.secretKey, &pt);
            *values = pt->GetPackedValue();
        }

        m_PreciseEnumResults.insert({ctId, values});
    }

    void ClientQueryProcessing::ProcessEnumApproxResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<double> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(static_cast<double>(value));
            }
        } else
        {
            std::istringstream ss(result);
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct;
            lbcrypto::Serial::Deserialize(ct, ss, lbcrypto::SerType::BINARY);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationEnumApprox.cc->Decrypt(ct, m_Client->m_InformationEnumApprox.keyPair.secretKey, &pt);
            *values = pt->GetRealPackedValue();
        }

        m_ApproxEnumResults.insert({ctId, values});
    }

    void ClientQueryProcessing::ProcessContinuousPreciseResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<double> >();

        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(static_cast<double>(value));
            }
        } else
        {
            std::istringstream ss(result);
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct;
            lbcrypto::Serial::Deserialize(ct, ss, lbcrypto::SerType::BINARY);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationContinuousPrecise.cc->Decrypt(ct, m_Client->m_InformationContinuousPrecise.keyPair.secretKey, &pt);

            const std::vector<int64_t> &results = pt->GetPackedValue();
            values->resize(result.size());

            for (size_t i = 0; i < results.size(); ++i)
                values->at(i) = static_cast<double>(results.at(i));
        }

        m_PreciseContinuousResults.insert({ctId, values});
    }

    void
    ClientQueryProcessing::ProcessContinuousApproxResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<double> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(static_cast<double>(value));
            }
        } else
        {
            std::istringstream ss(result);
            lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ct;
            lbcrypto::Serial::Deserialize(ct, ss, lbcrypto::SerType::BINARY);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationContinuousApprox.cc->Decrypt(ct, m_Client->m_InformationContinuousApprox.keyPair.secretKey, &pt);
            *values = pt->GetRealPackedValue();
        }

        m_ApproxContinuousResults.insert({ctId, values});
    }

    void ClientQueryProcessing::ProcessDistancePreciseResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<double> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(static_cast<double>(value));
            }
        } else
        {
            const Ciphertext ct = DeserializeFromString(result);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationDistancePrecise.cc->Decrypt(ct, m_Client->m_InformationDistancePrecise.keyPair.secretKey, &pt);

            const std::vector<int64_t> &results = pt->GetPackedValue();
            values->resize(result.size());

            for (size_t i = 0; i < results.size(); ++i)
                values->at(i) = static_cast<double>(results.at(i));
        }

        m_PreciseDistanceResults.insert({ctId, values});
    }

    void ClientQueryProcessing::ProcessDistanceApproxResults(int64_t ctId, const std::string &result)
    {
        auto values = std::make_shared<std::vector<double> >();
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            proto::IntegerPlaintext pt;
            pt.ParseFromString(result);

            for (const auto &value: pt.values())
            {
                values->push_back(static_cast<double>(value));
            }
        } else
        {
            const Ciphertext ct = DeserializeFromString(result);

            lbcrypto::Plaintext pt;
            m_Client->m_InformationDistanceApprox.cc->Decrypt(ct, m_Client->m_InformationDistanceApprox.keyPair.secretKey, &pt);
            *values = pt->GetRealPackedValue();
        }

        m_ApproxDistanceResults.insert({ctId, values});
    }

    void ClientQueryProcessing::WriteResultsToJson(const std::vector<PatientResultInfo> &results) const
    {
        nlohmann::json root;

        for (const auto &[id, provider, ctInfo]: results)
        {
            nlohmann::json jItem;
            jItem["patientId"] = id;
            jItem["provider"] = provider;

            for (const AttributeType type: AttributeTypes())
            {
                if (ctInfo.contains(type))
                {
                    const auto [ctId, ctIndex] = ctInfo.at(type);
                    switch (type)
                    {
                        case AttributeType::BOOLEAN:
                            jItem[kAttributeTypeNames.at(type)] = m_BooleanResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::ENUM_PRECISE:
                            jItem[kAttributeTypeNames.at(type)] = m_PreciseEnumResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::ENUM_APPROX:
                            jItem[kAttributeTypeNames.at(type)] = m_ApproxEnumResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::CONTINUOUS_PRECISE:
                            jItem[kAttributeTypeNames.at(type)] = m_PreciseContinuousResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::CONTINUOUS_APPROX:
                            jItem[kAttributeTypeNames.at(type)] = m_ApproxContinuousResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::DISTANCE_PRECISE:
                            jItem[kAttributeTypeNames.at(type)] = m_PreciseDistanceResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::DISTANCE_APPROX:
                            jItem[kAttributeTypeNames.at(type)] = m_ApproxDistanceResults.at(ctId)->at(ctIndex);
                            break;
                        case AttributeType::TYPE_COUNT:
                            PD_ASSERT(false, "Use of invalid attribute type")
                            break;
                    }
                }
            }

            root.push_back(std::move(jItem));
        }

        std::filesystem::path p = std::filesystem::current_path() / "test-results" / "query-results" / client::CommandLineParameters::GetInstance().GetTestId();
        create_directories(p);

        std::filesystem::path filePath = p / std::format("{}.json", client::CommandLineParameters::GetInstance().GetRunId());
        if (std::ofstream of(filePath); of.is_open())
        {
            of << root;
        }
    }
}
