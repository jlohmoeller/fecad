#include "pdshared/util/Utilities.hpp"

#include <openssl/evp.h>

#include "pdshared/config/Parameters.hpp"

namespace pat_disc {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::string GenerateUUIDv4()
    {
        std::stringstream ss;
        int i;
        ss << std::hex;
        for (i = 0; i < 8; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 4; i++)
        {
            ss << dis(gen);
        }
        ss << "-4";
        for (i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        ss << dis2(gen);
        for (i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";
        for (i = 0; i < 12; i++)
        {
            ss << dis(gen);
        };
        return ss.str();
    }


    std::string ExecuteShellCommand(const char *cmd)
    {
        std::array<char, 128> buffer{};
        std::string result;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        PD_ASSERT(pipe, "popen() failed");

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }

        return result;
    }

    void CalculateFileHash(const std::string &filePath, std::string &resultDigest)
    {
        EVP_MD_CTX *mdCtxt = EVP_MD_CTX_new();
        PD_ASSERT(mdCtxt != nullptr, "Message digest context creation failed")

        int rc = EVP_DigestInit_ex(mdCtxt, EVP_sha256(), nullptr);
        PD_ASSERT(rc == 1, "MD context initialization failed")

        constexpr std::streamsize bufferSize = 65536;
        const auto buffer = new char[bufferSize];

        std::ifstream fstream(filePath);
        while (fstream.good())
        {
            fstream.read(buffer, bufferSize);

            if (const std::streamsize bytesRead = fstream.gcount())
            {
                rc = EVP_DigestUpdate(mdCtxt, buffer, bytesRead);
                PD_ASSERT(rc == 1, "MD update failed")
            }
        }
        delete[] buffer;

        unsigned int size;
        const auto digestBuffer = new unsigned char[EVP_MD_size(EVP_sha256())];
        rc = EVP_DigestFinal_ex(mdCtxt, digestBuffer, &size);
        PD_ASSERT(rc == 1, "MD finalization failed")

        std::stringstream ss;
        for (unsigned int i = 0; i < size; i++)
        {
            ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<unsigned int>(digestBuffer[i]);
        }
        resultDigest = ss.str();
        delete[] digestBuffer;

        EVP_MD_CTX_free(mdCtxt);
    }

    uint64_t GetMaxRamUsageOfProcess()
    {
        std::ifstream fstream("/proc/self/status");
        std::regex pattern(R"(VmPeak:\s*(\d+)\s*kB)");

        uint64_t result = 0;

        std::string line;
        std::smatch match;
        while (std::getline(fstream, line))
        {
            if (std::regex_search(line, match, pattern))
            {
                std::string value = match[1];
                result = std::stoull(value) * 1024;
            }
        }

        return result;
    }

    std::string ReadFileContents(const std::string &filePath)
    {
        std::ifstream fileStream(filePath);
        std::stringstream ss;
        ss << fileStream.rdbuf();

        return ss.str();
    }

#if PD_DEBUG
    void DebugPrintDecrypted(const CryptoContext &cc, const Ciphertext &ct)
    {
        std::cout << "Debug Print" << std::endl;
        std::cout << cc << std::endl;
        //std::cout << ct << std::endl;

        lbcrypto::Plaintext pt;
        cc->Decrypt(ct, cc->GetPrivateKey(), &pt);
        pt->SetLength(10);

        std::cout << pt << std::endl;
    }
#endif
}
