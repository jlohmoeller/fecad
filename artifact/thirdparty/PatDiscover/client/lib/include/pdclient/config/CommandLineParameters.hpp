#ifndef COMMANDLINEPARAMETERS_CLIENT_HPP
#define COMMANDLINEPARAMETERS_CLIENT_HPP

namespace pat_disc::client {
    class CommandLineParameters
    {
    public:
        static CommandLineParameters &GetInstance();

        CommandLineParameters();

        void Parse(int argc, char **argv);

        [[nodiscard]] bool IsPlaintext() const;

        [[nodiscard]] bool IsGenerateRandomIds() const;

        [[nodiscard]] bool IsShutdownServers() const;

        [[nodiscard]] bool HasPatientDataFile() const;

        [[nodiscard]] const std::string &GetPatientDataFile() const;

        [[nodiscard]] const std::string &GetAttributeConfigFile() const;

        [[nodiscard]] const std::string &GetQueryFile() const;

        [[nodiscard]] const std::string &GetTestId() const;

        [[nodiscard]] const std::string &GetRunId() const;

        [[nodiscard]] int32_t GetUploadIterations() const;

    private:
        static CommandLineParameters *s_Instance;

        bool m_IsPlaintext = false;
        bool m_GenerateRandomIds = false;
        bool m_ShutdownServers = false;

        int32_t m_UploadIterations = 1;

        std::string m_TestId;
        std::string m_RunId;

        std::string m_AttributeConfigFile;
        std::string m_QueryFile;
        std::optional<std::string> m_PatientDataFile;
    };
}


#endif //COMMANDLINEPARAMETERS_HPP
