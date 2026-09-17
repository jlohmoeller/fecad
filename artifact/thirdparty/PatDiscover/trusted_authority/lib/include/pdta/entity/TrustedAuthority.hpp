#ifndef SERVER_TRUSTEDAUTHORITY_HPP
#define SERVER_TRUSTEDAUTHORITY_HPP

namespace pat_disc {
    struct ContextData
    {
        CryptoContext cc;
        lbcrypto::KeyPair<lbcrypto::DCRTPoly> kp;
    };

    struct ExtendedContextData : ContextData
    {
        std::shared_ptr<lbcrypto::BinFHEContext> binCC;
        lbcrypto::LWEPrivateKey binFhePrivateKey;
    };

    class TrustedAuthority
    {
    public:
        static void Initialize();

        [[nodiscard]] static std::ifstream GetCryptoContext(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetPublicKey(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetMultKey(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetSchemeSwitchingKey(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetAutomorphismKey(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetBinFheCryptoContext(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetBinFheRefreshKey(const AttributeType &type);

        [[nodiscard]] static std::ifstream GetBinFheSwitchKey(const AttributeType &type);

        [[nodiscard]] static std::shared_ptr<std::map<uint32_t, std::pair<std::ifstream, std::ifstream> > > GetBinFheBTKey(const AttributeType &type);

        [[nodiscard]] static std::stringstream CreateKeySwitchingKey(const AttributeType &strategy, const std::string &publicKey);

#if PD_DEBUG
        [[nodiscard]] static std::ifstream GetPrivateKey(const AttributeType &type);
#endif

    private:
        static void InitializeContextData(ContextData &data);

        static void InitializeExtendedContextData(ExtendedContextData &data);

        static void PersistContextData(AttributeType type, const ContextData &data);

        static void PersistExtendedContextData(AttributeType type, const ExtendedContextData &data);

        static std::filesystem::path GetBasePath(AttributeType type);

        static std::string GetPathPrefix(AttributeType type);

        static void ClearExtendedOpenFHEData(const ExtendedContextData &ctxData);
    };
}

#endif //SERVER_TRUSTEDAUTHORITY_HPP
