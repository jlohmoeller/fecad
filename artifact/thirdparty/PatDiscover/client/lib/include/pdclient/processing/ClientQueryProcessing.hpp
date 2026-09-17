#ifndef SERVER_CLIENTQUERYPROCESSING_HPP
#define SERVER_CLIENTQUERYPROCESSING_HPP

#include <pdproto/services.grpc.pb.h>

namespace pat_disc {
    class Client;

    struct PatientResultInfo
    {
        PatientResultInfo(const std::string &id, const std::string &provider, std::map<AttributeType, std::pair<int64_t, int64_t> > &&ctInfo): id(id), provider(provider),
            ctInfo(std::move(ctInfo))
        {
        }

        std::string id;
        std::string provider;
        std::map<AttributeType, std::pair<int64_t, int64_t> > ctInfo;
    };

    class ClientQueryProcessing
    {
    public:
        explicit ClientQueryProcessing(const Client *client,
                                       const std::unique_ptr<grpc::ClientReaderWriter<proto::ChunkedBytePayload, proto::QueryResponse> > &readWrite);

    private:
        void OnResultAvailable(const proto::QueryResult &res);

        void ProcessBooleanResults(int64_t ctId, const std::string& result);

        void ProcessEnumPreciseResults(int64_t ctId, const std::string &result);

        void ProcessEnumApproxResults(int64_t ctId, const std::string &result);

        void ProcessContinuousPreciseResults(int64_t ctId, const std::string &result);

        void ProcessContinuousApproxResults(int64_t ctId, const std::string &result);

        void ProcessDistancePreciseResults(int64_t ctId, const std::string &result);

        void ProcessDistanceApproxResults(int64_t ctId, const std::string &result);

        void WriteResultsToJson(const std::vector<PatientResultInfo> &results) const;

    private:
        const Client *m_Client;

        std::unordered_map<int64_t, IntegerEncoding> m_BooleanResults;
        std::unordered_map<int64_t, IntegerEncoding> m_PreciseEnumResults;
        std::unordered_map<int64_t, RealEncoding> m_ApproxEnumResults;
        std::unordered_map<int64_t, RealEncoding> m_PreciseContinuousResults;
        std::unordered_map<int64_t, RealEncoding> m_ApproxContinuousResults;
        std::unordered_map<int64_t, RealEncoding> m_PreciseDistanceResults;
        std::unordered_map<int64_t, RealEncoding> m_ApproxDistanceResults;
    };
}

#endif //SERVER_CLIENTQUERYPROCESSING_HPP
