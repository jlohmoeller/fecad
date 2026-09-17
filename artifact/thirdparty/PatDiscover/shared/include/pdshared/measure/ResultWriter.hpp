#ifndef RESULTWRITER_HPP
#define RESULTWRITER_HPP

namespace pat_disc {
    struct ResultWriterInfo
    {
        std::string entity;
        std::string testId;
        std::string runId;
        std::string attributeConfig;
        std::string query;
        std::string patientDataHash;
        bool plaintext = false;
        std::optional<uint64_t> patientCount;
        std::optional<uintmax_t> storageSize;
        std::optional<bool> toyParameters;
        uint64_t maxRamUsage;
    };

    class ResultWriter
    {
    public:
        static void WriteResults(const ResultWriterInfo &info);
    };
}

#endif //RESULTWRITER_HPP
