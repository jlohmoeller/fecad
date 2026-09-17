#include "pdta/entity/TrustedAuthority.hpp"

#include <pdshared/util/Serialization.hpp>
#include <pdta/config/CommandLineParameters.hpp>

#include "pdta/config/CryptoContextFactory.hpp"

namespace pat_disc {
    void TrustedAuthority::Initialize()
    {
        if (MatchableAttribute::GetAttributeCountForType(AttributeType::BOOLEAN))
        {
            PD_TRACE("Generating boolean crypto context...");
            ContextData data;
            data.cc = GenerateBooleanContext();
            InitializeContextData(data);
            PersistContextData(AttributeType::BOOLEAN, data);
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::ENUM_PRECISE))
        {
            PD_TRACE("Generating precise enum crypto context...");
            ContextData data;
            data.cc = GeneratePreciseEnumContext();
            InitializeContextData(data);
            PersistContextData(AttributeType::ENUM_PRECISE, data);
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::ENUM_APPROX))
        {
            PD_TRACE("Generating approx enum crypto context...");
            ContextData data;
            data.cc = GenerateApproxEnumContext();
            InitializeContextData(data);
            PersistContextData(AttributeType::ENUM_APPROX, data);
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::CONTINUOUS_PRECISE))
        {
            PD_TRACE("Generating precise continuous crypto context...");

            if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
            {
                ExtendedContextData data;
                data.cc = GeneratePreciseContinuousContext();

                InitializeExtendedContextData(data);
                PersistExtendedContextData(AttributeType::CONTINUOUS_PRECISE, data);
                ClearExtendedOpenFHEData(data); // Reduce memory footprint
            } else
            {
                ContextData data;
                data.cc = GeneratePreciseContinuousContext();
                InitializeContextData(data);
                PersistContextData(AttributeType::CONTINUOUS_PRECISE, data);
            }
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::CONTINUOUS_APPROX))
        {
            PD_TRACE("Generating approx continuous crypto context...");
            ContextData data;
            data.cc = GenerateApproxContinuousContext();
            InitializeContextData(data);
            PersistContextData(AttributeType::CONTINUOUS_APPROX, data);
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::DISTANCE_PRECISE))
        {
            PD_TRACE("Generating precise distance crypto context...");

            if (ta::CommandLineParameters::GetInstance().IsLegacyPreciseLessThan())
            {
                ExtendedContextData data;
                data.cc = GeneratePreciseDistanceContext();

                InitializeExtendedContextData(data);
                PersistExtendedContextData(AttributeType::DISTANCE_PRECISE, data);
                ClearExtendedOpenFHEData(data); // Reduce memory footprint
            } else
            {
                ContextData data;
                data.cc = GeneratePreciseDistanceContext();
                InitializeContextData(data);
                PersistContextData(AttributeType::DISTANCE_PRECISE, data);
            }
        }

        if (MatchableAttribute::GetAttributeCountForType(AttributeType::DISTANCE_APPROX))
        {
            PD_TRACE("Generating approx distance crypto context...");
            ExtendedContextData data;
            data.cc = GenerateApproxDistanceContext();
            InitializeContextData(data);
            PersistContextData(AttributeType::DISTANCE_APPROX, data);
        }

        // Clear all OpenFHE data, at this point everything is persisted to disk
        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalSumKeys();
        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalMultKeys();
        lbcrypto::CryptoContextImpl<lbcrypto::DCRTPoly>::ClearEvalAutomorphismKeys();
        lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::ReleaseAllContexts();
    }

    std::ifstream TrustedAuthority::GetCryptoContext(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "crypto-context.bin");
    }

    std::ifstream TrustedAuthority::GetPublicKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "public-key.bin");
    }

    std::ifstream TrustedAuthority::GetMultKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "mult-key.bin");
    }

    std::ifstream TrustedAuthority::GetSchemeSwitchingKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "scheme-switching-key.bin");
    }

    std::ifstream TrustedAuthority::GetAutomorphismKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "automorphism-key.bin");
    }

    std::ifstream TrustedAuthority::GetBinFheCryptoContext(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "bin-crypto-context.bin");
    }

    std::ifstream TrustedAuthority::GetBinFheRefreshKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "bin-refresh-key.bin");
    }

    std::ifstream TrustedAuthority::GetBinFheSwitchKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "bin-switch-key.bin");
    }

    std::shared_ptr<std::map<uint32_t, std::pair<std::ifstream, std::ifstream> > > TrustedAuthority::GetBinFheBTKey(const AttributeType &type)
    {
        const std::string digits = "0123456789";

        const auto result = std::make_shared<std::map<uint32_t, std::pair<std::ifstream, std::ifstream> > >();
        const std::filesystem::path btPath = GetBasePath(type) / "bin-bt";
        for (const auto &entry: std::filesystem::directory_iterator{btPath})
        {
            if (entry.is_regular_file())
            {
                // Find index from filename
                std::string filename = entry.path().filename();
                const size_t first = filename.find_first_of(digits);
                const size_t last = filename.find_first_not_of(digits);
                const int32_t index = std::stoi(filename.substr(first, last - first));

                std::string bsFileName = std::format("bs-key-{}.bin", index);
                std::string ksFileName = std::format("ks-key-{}.bin", index);

                result->insert({index, std::pair{std::ifstream(btPath / bsFileName), std::ifstream(btPath / ksFileName)}});
            }
        }

        return result;
    }

    std::stringstream TrustedAuthority::CreateKeySwitchingKey(const AttributeType &strategy, const std::string &publicKey)
    {
        CryptoContext cc;
        lbcrypto::PrivateKey<lbcrypto::DCRTPoly> sk;
        lbcrypto::PublicKey<lbcrypto::DCRTPoly> pk;

        // Load required OpenFHE data
        {
            std::ifstream fs(GetBasePath(strategy) / "crypto-context.bin");
            lbcrypto::Serial::Deserialize(cc, fs, lbcrypto::SerType::BINARY);
        }

        {
            std::istringstream iss(publicKey);
            lbcrypto::Serial::Deserialize(pk, iss, lbcrypto::SerType::BINARY);
        }

        {
            std::ifstream fs(GetBasePath(strategy) / "private-key.bin");
            lbcrypto::Serial::Deserialize(sk, fs, lbcrypto::SerType::BINARY);
        }

        cc = lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::GetFullContextByDeserializedContext(cc);
        auto ksKey = cc->ReKeyGen(sk, pk); // Generate Key Switching Key

        std::stringstream ss;
        lbcrypto::Serial::Serialize(ksKey, ss, lbcrypto::SerType::BINARY);

        lbcrypto::CryptoContextFactory<lbcrypto::DCRTPoly>::ReleaseAllContexts();

        return ss;
    }

    void TrustedAuthority::InitializeContextData(ContextData &data)
    {
        data.kp = data.cc->KeyGen();
        data.cc->EvalMultKeyGen(data.kp.secretKey);
    }

    void TrustedAuthority::InitializeExtendedContextData(ExtendedContextData &data)
    {
        InitializeContextData(data);

        lbcrypto::SchSwchParams contPrecParams = GetSchemeSwitchParamsForCompare();
        data.binFhePrivateKey = data.cc->EvalSchemeSwitchingSetup(contPrecParams);
        data.cc->EvalSchemeSwitchingKeyGen(data.kp, data.binFhePrivateKey);
        data.binCC = data.cc->GetBinCCForSchemeSwitch();

        // Not required, somehow happens implicitly during GetBinCCForSchemeSwitch. Standard OpenFHE nonsense
        //data.binCC->BTKeyGen(data.binFhePrivateKey);
    }

    void TrustedAuthority::PersistContextData(const AttributeType type, const ContextData &data)
    {
        const std::filesystem::path base = GetBasePath(type);
        create_directories(base);

        lbcrypto::Serial::SerializeToFile(base / "crypto-context.bin", data.cc, lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "public-key.bin", data.kp.publicKey, lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "private-key.bin", data.kp.secretKey, lbcrypto::SerType::BINARY);

        {
            std::ofstream fs(base / "mult-key.bin");
            data.cc->SerializeEvalMultKey(fs, lbcrypto::SerType::BINARY, data.kp.secretKey->GetKeyTag());
        }
    }

    void TrustedAuthority::PersistExtendedContextData(AttributeType type, const ExtendedContextData &data)
    {
        PersistContextData(type, data);

        const std::filesystem::path base = GetBasePath(type);
        lbcrypto::Serial::SerializeToFile(base / "bin-crypto-context.bin", data.binCC, lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "bin-private-key.bin", data.binFhePrivateKey, lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "scheme-switching-key.bin", data.cc->GetSwkFC(), lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "bin-refresh-key.bin", data.binCC->GetRefreshKey(), lbcrypto::SerType::BINARY);
        lbcrypto::Serial::SerializeToFile(base / "bin-switch-key.bin", data.binCC->GetSwitchKey(), lbcrypto::SerType::BINARY);

        {
            std::ofstream fs(base / "automorphism-key.bin");
            data.cc->SerializeEvalAutomorphismKey(fs, lbcrypto::SerType::BINARY, data.kp.secretKey->GetKeyTag());
        }

        const std::filesystem::path btPath = base / "bin-bt";
        create_directories(btPath);

        const auto map = data.binCC->GetBTKeyMap();
        for (const auto &[index, key]: *map)
        {
            std::string bsKeyFileName = std::format("bs-key-{}.bin", index);
            std::string ksKeyFileName = std::format("ks-key-{}.bin", index);

            lbcrypto::Serial::SerializeToFile(btPath / bsKeyFileName, key.BSkey, lbcrypto::SerType::BINARY);
            lbcrypto::Serial::SerializeToFile(btPath / ksKeyFileName, key.KSkey, lbcrypto::SerType::BINARY);
        }
    }

    std::filesystem::path TrustedAuthority::GetBasePath(const AttributeType type)
    {
        return std::filesystem::current_path() / "ta-storage" / GetPathPrefix(type);
    }

    std::string TrustedAuthority::GetPathPrefix(const AttributeType type)
    {
        switch (type)
        {
            case AttributeType::BOOLEAN:
                return "boolean";
            case AttributeType::ENUM_PRECISE:
                return "enum-precise";
            case AttributeType::ENUM_APPROX:
                return "enum-approx";
            case AttributeType::CONTINUOUS_PRECISE:
                return "continuous-precise";
            case AttributeType::CONTINUOUS_APPROX:
                return "continuous-approx";
            case AttributeType::DISTANCE_PRECISE:
                return "distance-precise";
            case AttributeType::DISTANCE_APPROX:
                return "distance-approx";
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Use of invalid attribute type")
                break;
        }

        return "";
    }

    void TrustedAuthority::ClearExtendedOpenFHEData(const ExtendedContextData &ctxData)
    {
        ctxData.cc->GetBinCCForSchemeSwitch()->ClearBTKeys();
        ctxData.cc->SetBinCCForSchemeSwitch(nullptr);
    }

#if PD_DEBUG
    std::ifstream TrustedAuthority::GetPrivateKey(const AttributeType &type)
    {
        return std::ifstream(GetBasePath(type) / "private-key.bin");
    }
#endif
}
