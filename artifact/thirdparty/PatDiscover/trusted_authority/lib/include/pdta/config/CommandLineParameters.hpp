#ifndef COMMANDLINEPARAMETERS_TA_HPP
#define COMMANDLINEPARAMETERS_TA_HPP

namespace pat_disc::ta {
    class CommandLineParameters
    {
    public:
        static CommandLineParameters &GetInstance();

        CommandLineParameters();

        void Parse(int argc, char **argv);

        [[nodiscard]] bool IsUseCachedData() const;

        [[nodiscard]] bool IsUseToyParameters() const;

        [[nodiscard]] bool IsLegacySignApproximation() const;

        [[nodiscard]] bool IsLegacyPreciseLessThan() const;

        [[nodiscard]] const std::string &GetAttributeConfigFile() const;

        [[nodiscard]] const std::string &GetTestId() const;

        [[nodiscard]] const std::string &GetRunId() const;

    private:
        static CommandLineParameters *s_Instance;

        bool m_UseCachedData = false;
        bool m_UseToyParameters = false;
        bool m_LegacySignApproximation = false;
        bool m_LegacyPreciseLessThan = false;

        std::string m_AttributeConfigFile;
        std::string m_TestId;
        std::string m_RunId;
    };
}


#endif //COMMANDLINEPARAMETERS_HPP
