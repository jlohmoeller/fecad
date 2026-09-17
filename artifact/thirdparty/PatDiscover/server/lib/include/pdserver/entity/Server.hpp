#ifndef SERVER_SERVER_HPP
#define SERVER_SERVER_HPP

#include <pdproto/data_transfer_objects.pb.h>

#include "pdserver/grpc/TrustedAuthorityClient.hpp"

namespace pat_disc {
    class Server
    {
    public:
        static Server &GetInstance()
        {
            return *s_Instance;
        }

        void Initialize();

        static void Shutdown();

        void ProcessQuery(const proto::QueryRequest &req, grpc::ServerReaderWriter<proto::QueryResponse, proto::ChunkedBytePayload> *readWrite);

    private:
        static void LoadKeys(CryptoContext &cc, AttributeType type, const TrustedAuthorityClient &client);

        std::string BooleanMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string
        PreciseEnumMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string ApproxEnumMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                       const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string PreciseContinuousMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                              const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string ApproxContinuousMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                             const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string PreciseDistanceMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                            const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        std::string ApproxDistanceMatching(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                           const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey);

        static Ciphertext GenericQueryRec(const CryptoContext &cc, const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                          const std::function<Ciphertext(const proto::QueryAttributeData &, const std::string &)> &onMatch);

        static IntegerEncoding GenericQueryPlainIntRec(const proto::CombineGroup &group, const std::map<std::string, std::string> &data,
                                                       const std::function<IntegerEncoding(const proto::QueryAttributeData &, const std::string &)> &onMatch);

    private:
        static Server *s_Instance;

        CryptoContext m_BooleanCC;
        CryptoContext m_EnumPreciseCC;
        CryptoContext m_EnumApproxCC;
        CryptoContext m_ContinuousPreciseCC;
        CryptoContext m_ContinuousApproxCC;
        CryptoContext m_DistancePreciseCC;
        CryptoContext m_DistanceApproxCC;
    };
}

#endif //SERVER_SERVER_HPP
