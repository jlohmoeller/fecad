#ifndef SERVER_CLIENT_HPP
#define SERVER_CLIENT_HPP

#include <pdclient/grpc/PatientDataServerClient.hpp>
#include <pdclient/grpc/TrustedAuthorityClient.hpp>

namespace pat_disc {
    class QueryBuilder;
    typedef std::variant<int64_t, double, Position<int64_t>, Position<double>> VectorAttributeData;

    struct ClientContextInfo
    {
        CryptoContext cc;
        lbcrypto::EvalKey<lbcrypto::DCRTPoly> preKey;       // Proxy Re-Encryption
        lbcrypto::KeyPair<lbcrypto::DCRTPoly> keyPair;      // Client key pair
        lbcrypto::PublicKey<lbcrypto::DCRTPoly> taPubKey;   // TA public key
    };

    class Client
    {
    public:
        explicit Client(const std::string &providerName, const std::shared_ptr<grpc::Channel> &channel);

        explicit Client(std::string &&providerName, const std::shared_ptr<grpc::Channel> &channel);

        void Initialize();

        uint64_t AddPatientData(const std::string &filePath);

        void ExecuteQuery(const std::string &filePath) const;

        void ExecuteSqlQuery(const std::string &query) const;

        void Shutdown() const;

    private:
        static void
        RetrieveStrategyInformation(ClientContextInfo &strategyInformation, const AttributeType &type, const TrustedAuthorityClient &client);

        static void SerializeKey(AttributeType type, const lbcrypto::EvalKey<lbcrypto::DCRTPoly> &ksKey, proto::KeySwitchingKeys *keys);

        [[nodiscard]] std::string CreateCiphertextBoolean(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextEnumPrecise(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextEnumApprox(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextContinuousPrecise(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextContinuousApprox(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextDistancePrecise(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] std::string CreateCiphertextDistanceApprox(const std::vector<VectorAttributeData> &vec) const;

        [[nodiscard]] static std::string CreateIntegerPlaintext(const std::vector<VectorAttributeData> &vec);

        [[nodiscard]] static std::string CreateDistancePlaintext(const std::vector<VectorAttributeData> &vec);

        void RunQuery(QueryBuilder &qb) const;

        friend class QueryAttributeData;
        friend class ClientQueryProcessing;

    private:
        ClientContextInfo m_InformationBoolean;
        ClientContextInfo m_InformationEnumPrecise;
        ClientContextInfo m_InformationEnumApprox;
        ClientContextInfo m_InformationContinuousPrecise;
        ClientContextInfo m_InformationContinuousApprox;
        ClientContextInfo m_InformationDistancePrecise;
        ClientContextInfo m_InformationDistanceApprox;

        PatientDataServerClient m_Client;

        std::string m_ProviderName;
    };
}

#endif //SERVER_CLIENT_HPP
