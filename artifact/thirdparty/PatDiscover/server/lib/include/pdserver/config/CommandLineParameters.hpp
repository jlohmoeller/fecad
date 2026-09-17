#ifndef COMMANDLINEPARAMETERS_SERVER_HPP
#define COMMANDLINEPARAMETERS_SERVER_HPP

namespace pat_disc::server {
    class CommandLineParameters
    {
    public:
        static CommandLineParameters &GetInstance();

        CommandLineParameters();

        void Parse(int argc, char **argv);

        [[nodiscard]] bool IsPlaintext() const;

        [[nodiscard]] bool IsDropTablesOnStart() const;

        [[nodiscard]] const std::string &GetDatabaseFile() const;

        [[nodiscard]] const std::string &GetAttributeConfigFile() const;

        [[nodiscard]] const std::string &GetTestId() const;

        [[nodiscard]] const std::string &GetRunId() const;

    private:
        static CommandLineParameters *s_Instance;

        bool m_IsPlaintext = false;
        bool m_DropTablesOnStart = false;

        std::string m_RunId;
        std::string m_TestId;

        std::string m_DatabaseFile;
        std::string m_AttributeConfigFile;
    };
}

#endif // COMMANDLINEPARAMETERS_SERVER_HPP
