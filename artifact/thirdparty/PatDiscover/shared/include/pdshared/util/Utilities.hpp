#ifndef SERVER_UTILITIES_HPP
#define SERVER_UTILITIES_HPP

#include <pdshared/model/DataTransferObjects.hpp>

#include "pdshared/model/MatchableAttribute.hpp"

namespace pat_disc {
    std::string ExecuteShellCommand(const char *cmd);

    std::string GenerateUUIDv4();

    template<typename T>
    void PrintVector(const std::shared_ptr<std::vector<T> > &vec, uint32_t printLength = 0)
    {
        uint32_t max;
        if (printLength == 0)
        {
            max = vec->size();
        } else
        {
            max = std::min((uint32_t) vec->size(), printLength);
        }

        for (uint32_t i = 0; i < max; i++)
        {
            std::cout << vec->at(i) << ", ";
        }

        std::cout << std::endl;
    }

    void CalculateFileHash(const std::string &filePath, std::string &resultDigest);

    // This function is Linux-specific, does not work on other operating systems
    uint64_t GetMaxRamUsageOfProcess();

    std::string ReadFileContents(const std::string& filePath);

#ifdef PD_DEBUG
    void DebugPrintDecrypted(const CryptoContext& cc, const Ciphertext& ct);
#endif
}


#endif //SERVER_UTILITIES_HPP
