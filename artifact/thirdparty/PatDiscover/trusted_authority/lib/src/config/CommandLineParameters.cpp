#include "pdta/config/CommandLineParameters.hpp"

#include <boost/program_options.hpp>

namespace pat_disc::ta {
    CommandLineParameters *CommandLineParameters::s_Instance = new CommandLineParameters();

    CommandLineParameters &CommandLineParameters::GetInstance()
    {
        return *s_Instance;
    }

    CommandLineParameters::CommandLineParameters() = default;

    void CommandLineParameters::Parse(const int argc, char **argv)
    {
        namespace po = boost::program_options;

        po::options_description desc("Allowed Options");
        desc.add_options()
                ("help", "Produce help message")
                ("use-cached-data,C", "Use pre-generated data")
                ("use-toy-parameters", "Make use of OpenFHE Toy Security Parameters")
                ("legacy-sign-approx", "Use the legacy sign approximation")
                ("legacy-precise-less-than", "Use the legacy precise less than")
                ("test-id", po::value<std::string>(), "Test test ID")
                ("run-id", po::value<std::string>(), "Test run ID")
                ("attribute-config-file", po::value<std::string>(), "Path to the file configuring the attributes");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.contains("help"))
        {
            std::cout << desc << std::endl;
            exit(0);
        }

        if (vm.contains("use-cached-data"))
        {
            m_UseCachedData = true;
        }

        if (vm.contains("use-toy-parameters"))
        {
            m_UseToyParameters = true;
        }

        if (vm.contains("legacy-sign-approx"))
        {
            m_LegacySignApproximation = true;
        }

        if (vm.contains("legacy-precise-less-than"))
        {
            m_LegacyPreciseLessThan = true;
        }

        if (vm.contains("test-id"))
        {
            m_TestId = vm["test-id"].as<std::string>();
        }

        if (vm.contains("run-id"))
        {
            m_RunId = vm["run-id"].as<std::string>();
        }

        if (vm.contains("attribute-config-file"))
        {
            m_AttributeConfigFile = vm["attribute-config-file"].as<std::string>();
        }
    }

    bool CommandLineParameters::IsUseCachedData() const
    {
        return m_UseCachedData;
    }

    bool CommandLineParameters::IsUseToyParameters() const
    {
        return m_UseToyParameters;
    }

    bool CommandLineParameters::IsLegacySignApproximation() const
    {
        return m_LegacySignApproximation;
    }

    bool CommandLineParameters::IsLegacyPreciseLessThan() const
    {
        return m_LegacyPreciseLessThan;
    }

    const std::string &CommandLineParameters::GetAttributeConfigFile() const
    {
        return m_AttributeConfigFile;
    }

    const std::string &CommandLineParameters::GetTestId() const
    {
        return m_TestId;
    }

    const std::string &CommandLineParameters::GetRunId() const
    {
        return m_RunId;
    }
}
