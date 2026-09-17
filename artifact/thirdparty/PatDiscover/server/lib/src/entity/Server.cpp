#include "pdserver/entity/Server.hpp"

#include <sstream>

#include <grpc++/create_channel.h>
#include <pdserver/processing/QueryProcessingPrimitive.hpp>
#include <pdshared/grpc/CostMetricInterceptor.hpp>

#include <pdshared/util/Serialization.hpp>
#include <pdshared/util/Utilities.hpp>

#include "pdserver/config/CommandLineParameters.hpp"
#include "pdserver/data/DatabaseWrapper.hpp"
#include "pdserver/grpc/TrustedAuthorityClient.hpp"
#include "pdserver/processing/QueryProcessing.hpp"

namespace pat_disc {
    Server *Server::s_Instance = new Server;

    struct ThreadInfo
    {
        int32_t currentRc;
        sqlite3_stmt *statement;
        std::mutex *databaseMutex;
        std::mutex *grpcMutex;

        AttributeType type;
        const std::vector<std::string> *attributes;
        const Server *serverInstance;
        const proto::CombineGroup *combineGroup;
        const lbcrypto::EvalKey<lbcrypto::DCRTPoly> *ksKey;
        std::function<std::string(const proto::CombineGroup &, const std::map<std::string, std::string> &, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &)> computeFunc;
        grpc::ServerReaderWriter<proto::QueryResponse, proto::ChunkedBytePayload> *writer;
    };

    // Parallel database access
    void ComputeThread(ThreadInfo *info)
    {
        std::map<std::string, std::string> data;

        while (true)
        {
            info->databaseMutex->lock(); // Lock database access
            if (info->currentRc != SQLITE_ROW && info->currentRc != SQLITE_OK) // No more ciphertexts available or error
            {
                info->databaseMutex->unlock(); // Unlock and stop thread
                break;
            }

            // Fetch next batch of ciphertexts
            const int64_t ctId = DatabaseWrapper::FetchNextCiphertext(info->statement, *info->attributes, data, info->currentRc);

            if (info->currentRc != SQLITE_ROW) // No more ciphertexts or error
            {
                info->databaseMutex->unlock(); // Unlock and stop thread
                break;
            }
            info->databaseMutex->unlock(); // Unlock database access

            // Perform matching
            auto result = info->computeFunc(*info->combineGroup, data, *info->ksKey);

            // Assemble result
            proto::QueryResponse response;
            proto::QueryResult *res = response.mutable_result();
            res->set_attribute_type(static_cast<proto::AttributeType>(info->type));
            res->set_ct_id(ctId);
            res->set_result(result);

            info->grpcMutex->lock(); // Lock gRPC access
            info->writer->Write(response); // Write result
            info->grpcMutex->unlock(); // Unlock gRPC access

            PD_TRACE("Sent query response. Result size is: {}", result.size());
            data.clear();
        }
    }

    void Server::Initialize()
    {
        DatabaseWrapper::GetInstance().Initialize();

        if (!server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            // Clear any existing OpenFHE data
            lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalMultKeys();
            lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalAutomorphismKeys();
            lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalSumKeys();
            lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::ReleaseAllContexts();

            std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface> > creators;
            creators.push_back(std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>(new ClientCostMetricInterceptorFactory()));

            auto credOptions = grpc::SslCredentialsOptions();
            credOptions.pem_root_certs = ReadFileContents("data/certificates/ca-cert.pem");
            credOptions.pem_private_key = ReadFileContents("data/certificates/server-key.pem");
            credOptions.pem_cert_chain = ReadFileContents("data/certificates/server-cert.pem");
            const auto channelCredentials = SslCredentials(credOptions);

            // Create gRPC channel to TA
            const grpc::ChannelArguments args;
            const auto channel = CreateCustomChannelWithInterceptors("localhost:50001", channelCredentials, args, std::move(creators));
            const TrustedAuthorityClient taClient(channel);

            PD_TRACE("Loading keys for server from TA...");
            LoadKeys(m_BooleanCC, AttributeType::BOOLEAN, taClient);
            LoadKeys(m_EnumPreciseCC, AttributeType::ENUM_PRECISE, taClient);
            LoadKeys(m_EnumApproxCC, AttributeType::ENUM_APPROX, taClient);
            LoadKeys(m_ContinuousPreciseCC, AttributeType::CONTINUOUS_PRECISE, taClient);
            LoadKeys(m_ContinuousApproxCC, AttributeType::CONTINUOUS_APPROX, taClient);
            LoadKeys(m_DistancePreciseCC, AttributeType::DISTANCE_PRECISE, taClient);
            LoadKeys(m_DistanceApproxCC, AttributeType::DISTANCE_APPROX, taClient);
        }

        QueryProcessing::Init();
    }

    void Server::ProcessQuery(const proto::QueryRequest &req, grpc::ServerReaderWriter<proto::QueryResponse, proto::ChunkedBytePayload> *readWrite)
    {
        const proto::QueryData &queryData = req.data();
        const proto::KeySwitchingKeys &switchingKeys = req.keys();

        // Deserialize Key switching keys
        std::map<AttributeType, lbcrypto::EvalKey<lbcrypto::DCRTPoly> > keyMap;
        for (const auto &entry: switchingKeys.keys())
        {
            std::istringstream ss(entry.key());
            lbcrypto::EvalKey<lbcrypto::DCRTPoly> k;
            lbcrypto::Serial::Deserialize(k, ss, lbcrypto::SerType::BINARY);

            keyMap.insert({static_cast<AttributeType>(entry.attribute_type()), k});
        }

        std::set<AttributeType> usedQueryTypes;
        for (const auto &entry: queryData.entries())
        {
            const auto type = static_cast<AttributeType>(entry.attribute_type());
            usedQueryTypes.insert(type);

            // Start ciphertext retrieval, get total number of ciphertexts
            int32_t resultCount;
            std::vector<std::string> attributes;
            auto stmt = DatabaseWrapper::GetInstance().StartRetrieveCiphertextsOfAttributeType(type, attributes, resultCount);

            std::mutex databaseMutex;
            std::mutex grpcMutex;

            // Assemble thread information
            ThreadInfo tInfo{};
            tInfo.type = type;
            tInfo.attributes = &attributes;
            tInfo.statement = stmt;
            tInfo.currentRc = SQLITE_OK;
            tInfo.serverInstance = this;
            tInfo.databaseMutex = &databaseMutex;
            tInfo.grpcMutex = &grpcMutex;
            tInfo.combineGroup = &entry.group();
            tInfo.ksKey = &keyMap[type];
            tInfo.writer = readWrite;

            switch (type)
            {
                case AttributeType::BOOLEAN:
                    tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                               const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                    {
                        return this->BooleanMatching(group, data, ksKey);
                    };
                    break;
                case AttributeType::ENUM_PRECISE:
                    tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                               const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                    {
                        return this->PreciseEnumMatching(group, data, ksKey);
                    };
                    break;
                case AttributeType::ENUM_APPROX:
                {
                    if (server::CommandLineParameters::GetInstance().IsPlaintext())
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->PreciseEnumMatching(group, data, ksKey);
                        };
                    } else
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->ApproxEnumMatching(group, data, ksKey);
                        };
                    }
                }
                break;
                case AttributeType::CONTINUOUS_PRECISE:
                    tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                               const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                    {
                        return this->PreciseContinuousMatching(group, data, ksKey);
                    };
                    break;
                case AttributeType::CONTINUOUS_APPROX:
                {
                    if (server::CommandLineParameters::GetInstance().IsPlaintext())
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->PreciseContinuousMatching(group, data, ksKey);
                        };
                    } else
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->ApproxContinuousMatching(group, data, ksKey);
                        };
                    }
                }
                break;
                case AttributeType::DISTANCE_PRECISE:
                    tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                               const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                    {
                        return this->PreciseDistanceMatching(group, data, ksKey);
                    };
                    break;
                case AttributeType::DISTANCE_APPROX:
                {
                    if (server::CommandLineParameters::GetInstance().IsPlaintext())
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->PreciseDistanceMatching(group, data, ksKey);
                        };
                    } else
                    {
                        tInfo.computeFunc = [this](const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                   const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey) -> std::string
                        {
                            return this->ApproxDistanceMatching(group, data, ksKey);
                        };
                    }
                }
                break;
                case AttributeType::TYPE_COUNT:
                    PD_ASSERT(false, "Use of invalid attribute type")
                    break;
            }

            // Minimum of ciphertext count and CPU cores
            int32_t threadCount = std::min(resultCount, static_cast<int32_t>(std::thread::hardware_concurrency()));
            QueryProcessing::SetThreadCount(threadCount); // Set for parallel sign eval

            PD_INFO("Spawning {} threads for database processing.", threadCount);

            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (int32_t i = 0; i < threadCount; i++)
            {
                threads.emplace_back(ComputeThread, &tInfo); // Start thread
            }

            for (auto &t: threads)
            {
                t.join();
            }

            DatabaseWrapper::GetInstance().FinalizeStatement(stmt);
        }

        // Fetch patient and provider info from database
        DatabaseWrapper::GetInstance().RetrievePatientData(usedQueryTypes, [&readWrite](const PatientDataTable &data) -> void
        {
            proto::QueryResponse response;
            proto::PatientToCiphertexts *to = response.mutable_patient_info();
            to->set_patient_id(data.id);
            to->set_provider(data.provider);

            for (const auto &[type, ctInfo]: data.ctInfo)
            {
                proto::AttrTypeCtId *ctId = to->add_ciphertext_ids();
                ctId->set_attribute_type(static_cast<proto::AttributeType>(type));
                ctId->set_ct_id(ctInfo.first);
                ctId->set_ct_index(ctInfo.second);
            }

            readWrite->Write(response);
        });
    }

    void Server::Shutdown()
    {
        DatabaseWrapper::GetInstance().Close();
    }

    void Server::LoadKeys(lbcrypto::CryptoContext<lbcrypto::DCRTPoly> &cc, const AttributeType type, const TrustedAuthorityClient &client)
    {
        if (!MatchableAttribute::GetAttributeCountForType(type)) // Skip if no attributes for this attribute type present
            return;

        cc = client.GetCryptoContext(type);
        cc = lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::GetFullContextByDeserializedContext(cc);

        // Add private key to crypto context in debug config
#if PD_DEBUG
        cc->SetPrivateKey(client.GetPrivateKey(type));
#endif

        client.GetMultKey(type, cc);
    }

    std::string Server::BooleanMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                        const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const IntegerEncoding result = GenericQueryPlainIntRec(group, data, [](const auto &attr, const std::string &data) -> IntegerEncoding
            {
                const IntegerEncoding dataCt = DeserializePlainIntFromString(data);
                const IntegerEncoding queryCt = DeserializePlainIntFromMessage(attr.single_int_plain());

                return QueryProcessingPrimitive::EnumMatching(dataCt, queryCt);
            });

            return SerializePlainIntToString(*result);
        }

        Ciphertext result = GenericQueryRec(m_BooleanCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            const Ciphertext dataCt = DeserializeFromString(data);
            const Ciphertext queryCt = DeserializeFromString(attr.single_ciphertext());

            return QueryProcessing::BooleanMatching(m_BooleanCC, dataCt, queryCt);
        });
        result = m_BooleanCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string
    Server::PreciseEnumMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const IntegerEncoding result = GenericQueryPlainIntRec(group, data, [](const auto &attr, const std::string &data) -> IntegerEncoding
            {
                const IntegerEncoding dataCt = DeserializePlainIntFromString(data);
                const IntegerEncoding queryCt = DeserializePlainIntFromMessage(attr.single_int_plain());

                return QueryProcessingPrimitive::EnumMatching(dataCt, queryCt);
            });

            return SerializePlainIntToString(*result);
        }

        Ciphertext result = GenericQueryRec(m_EnumPreciseCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            const Ciphertext dataCt = DeserializeFromString(data);
            const Ciphertext queryCt = DeserializeFromString(attr.single_ciphertext());

            return QueryProcessing::EqualityMatchingPrecise(m_EnumPreciseCC, dataCt, queryCt);
        });
        result = m_EnumPreciseCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string
    Server::ApproxEnumMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            PD_ASSERT(false, "Plaintext is not supported for approximate enum matching since plaintext matching is always precise.")
        }

        Ciphertext result = GenericQueryRec(m_EnumApproxCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            const Ciphertext dataCt = DeserializeFromString(data);
            const Ciphertext queryCt = DeserializeFromString(attr.single_ciphertext());

            return QueryProcessing::EqualityMatchingApprox(m_EnumApproxCC, dataCt, queryCt);
        });
        result = m_EnumApproxCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string Server::PreciseContinuousMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                  const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const IntegerEncoding result = GenericQueryPlainIntRec(group, data, [](const auto &attr, const std::string &data) -> IntegerEncoding
            {
                const IntegerEncoding dataCt = DeserializePlainIntFromString(data);
                const proto::RangeQuery &r = attr.range();
                const IntegerEncoding queryLb = DeserializePlainIntFromMessage(r.plain_lower());
                const IntegerEncoding queryUb = DeserializePlainIntFromMessage(r.plain_upper());

                return QueryProcessingPrimitive::ContinuousMatching(dataCt, queryLb, queryUb);
            });

            return SerializePlainIntToString(*result);
        }

        Ciphertext result = GenericQueryRec(m_ContinuousPreciseCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            const Ciphertext dataCt = DeserializeFromString(data);
            const proto::RangeQuery &r = attr.range();
            const Ciphertext queryLb = DeserializeFromString(r.ciphertext_lower());
            const Ciphertext queryUb = DeserializeFromString(r.ciphertext_upper());

            return QueryProcessing::ComparisonMatchingPrecise(m_ContinuousPreciseCC, dataCt, queryLb, queryUb);
        });
        result = m_ContinuousPreciseCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string Server::ApproxContinuousMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                 const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            PD_ASSERT(false, "Plaintext is not supported for approximate range matching since plaintext matching is always precise.")
        }

        Ciphertext result = GenericQueryRec(m_ContinuousApproxCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            const Ciphertext dataCt = DeserializeFromString(data);
            const proto::RangeQuery &r = attr.range();
            const Ciphertext queryLb = DeserializeFromString(r.ciphertext_lower());
            const Ciphertext queryUb = DeserializeFromString(r.ciphertext_upper());

            return QueryProcessing::ComparisonMatchingApprox(m_ContinuousApproxCC, dataCt, queryLb, queryUb);
        });
        result = m_ContinuousApproxCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string Server::PreciseDistanceMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const IntegerEncoding result = GenericQueryPlainIntRec(group, data, [](const auto &attr, const std::string &data) -> IntegerEncoding
            {
                proto::Position posData;
                posData.ParseFromString(data);

                const IntegerEncoding xData = DeserializePlainIntFromMessage(posData.plain_x());
                const IntegerEncoding yData = DeserializePlainIntFromMessage(posData.plain_y());
                const IntegerEncoding zData = DeserializePlainIntFromMessage(posData.plain_z());

                const proto::PositionQuery &r = attr.position();
                const IntegerEncoding xQuery = DeserializePlainIntFromMessage(r.plain_x());
                const IntegerEncoding yQuery = DeserializePlainIntFromMessage(r.plain_y());
                const IntegerEncoding zQuery = DeserializePlainIntFromMessage(r.plain_z());
                const IntegerEncoding upperBound = DeserializePlainIntFromMessage(r.plain_upper());

                return QueryProcessingPrimitive::DistanceMatching(xData, yData, zData, xQuery, yQuery, zQuery, upperBound);
            });

            return SerializePlainIntToString(*result);
        }

        Ciphertext result = GenericQueryRec(m_DistancePreciseCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            proto::Position posData;
            posData.ParseFromString(data);

            const Ciphertext xData = DeserializeFromString(posData.ciphertext_x());
            const Ciphertext yData = DeserializeFromString(posData.ciphertext_y());
            const Ciphertext zData = DeserializeFromString(posData.ciphertext_z());

            const proto::PositionQuery &p = attr.position();
            const Ciphertext xQuery = DeserializeFromString(p.ciphertext_x());
            const Ciphertext yQuery = DeserializeFromString(p.ciphertext_y());
            const Ciphertext zQuery = DeserializeFromString(p.ciphertext_z());
            const Ciphertext upperBound = DeserializeFromString(p.ciphertext_upper());

            return QueryProcessing::DistanceMatchingPrecise(m_DistancePreciseCC, xData, yData, zData, xQuery, yQuery, zQuery, upperBound);
        });
        result = m_DistancePreciseCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    std::string Server::ApproxDistanceMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                               const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey)
    {
        if (server::CommandLineParameters::GetInstance().IsPlaintext())
        {
            PD_ASSERT(false, "Plaintext is not supported for approximate distance matching since plaintext matching is always precise.")
        }

        Ciphertext result = GenericQueryRec(m_DistanceApproxCC, group, data, [this](const auto &attr, const std::string &data) -> Ciphertext
        {
            proto::Position posData;
            posData.ParseFromString(data);

            const Ciphertext xData = DeserializeFromString(posData.ciphertext_x());
            const Ciphertext yData = DeserializeFromString(posData.ciphertext_y());
            const Ciphertext zData = DeserializeFromString(posData.ciphertext_z());

            const proto::PositionQuery &p = attr.position();
            const Ciphertext xQuery = DeserializeFromString(p.ciphertext_x());
            const Ciphertext yQuery = DeserializeFromString(p.ciphertext_y());
            const Ciphertext zQuery = DeserializeFromString(p.ciphertext_z());
            const Ciphertext upperBound = DeserializeFromString(p.ciphertext_upper());

            return QueryProcessing::DistanceMatchingApprox(m_DistanceApproxCC, xData, yData, zData, xQuery, yQuery, zQuery, upperBound);
        });
        result = m_DistanceApproxCC->ReEncrypt(result, ksKey);

        return SerializeToString(result);
    }

    Ciphertext Server::GenericQueryRec/* NOLINT(*-no-recursion) */(const CryptoContext &cc, const proto::CombineGroup &group,
                                                                   const std::map<std::string, std::string> &data,
                                                                   const std::function<Ciphertext(const proto::QueryAttributeData &, const std::string &)> &onMatch)
    {
        std::vector<Ciphertext> matchingResults;
        if (group.has_attribute_data_list())
        {
            for (const auto &attr: group.attribute_data_list().list())
            {
                const auto &pa = attr.attribute();
                const auto res = onMatch(attr, data.at(pa)); // Perform actual matching

                if (attr.invert_result())
                {
                    matchingResults.push_back(QueryProcessing::Not(cc, res));
                } else
                {
                    matchingResults.push_back(res);
                }
            }
        } else
        {
            // Recursively traverse query structure
            for (const auto &g: group.groups().list())
            {
                matchingResults.push_back(GenericQueryRec(cc, g, data, onMatch));
            }
        }

        // Aggregate results of combine group
        switch (group.op())
        {
            case proto::CombineOperator::AND:
                return QueryProcessing::And(cc, matchingResults);
            case proto::CombineOperator::OR:
                return QueryProcessing::Or(cc, matchingResults);
            case proto::CombineOperator::SUM:
                return QueryProcessing::Sum(cc, matchingResults);
            default:
                PD_ASSERT(false, "Invalid combine operator used");
        }

        return {};
    }

    IntegerEncoding Server::GenericQueryPlainIntRec(const proto::CombineGroup &group, const std::map<std::string, std::string> &data, // NOLINT(*-no-recursion)
                                                    const std::function<IntegerEncoding(const proto::QueryAttributeData &, const std::string &)> &onMatch)
    {
        std::vector<IntegerEncoding> matchingResults;
        if (group.has_attribute_data_list())
        {
            for (const auto &attr: group.attribute_data_list().list())
            {
                const auto &pa = attr.attribute();
                const auto res = onMatch(attr, data.at(pa));

                if (attr.invert_result())
                {
                    matchingResults.push_back(QueryProcessingPrimitive::Not(res));
                } else
                {
                    matchingResults.push_back(res);
                }
            }
        } else
        {
            // Recursively traverse query structure
            for (const auto &g: group.groups().list())
            {
                matchingResults.push_back(GenericQueryPlainIntRec(g, data, onMatch));
            }
        }

        // Aggregate results of combine group
        switch (group.op())
        {
            case proto::CombineOperator::AND:
                return QueryProcessingPrimitive::And(matchingResults);
            case proto::CombineOperator::OR:
                return QueryProcessingPrimitive::Or(matchingResults);
            case proto::CombineOperator::SUM:
                return QueryProcessingPrimitive::Sum(matchingResults);
            default:
                PD_ASSERT(false, "Invalid combine operator used");
        }

        return {};
    }
}
