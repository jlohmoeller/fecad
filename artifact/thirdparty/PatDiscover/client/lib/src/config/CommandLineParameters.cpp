#include "pdclient/config/CommandLineParameters.hpp"

#include <boost/program_options.hpp>

namespace pat_disc::client {
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
                ("plaintext,P", "Perform computations in plaintext")
                ("random-ids,R", "Use random IDs when inserting patient data")
                ("patient-data-file", po::value<std::string>(), "Path to the file of patient data to upload")
                ("attribute-config-file", po::value<std::string>(), "Path to the file configuring the attributes")
                ("query-file", po::value<std::string>(), "Path to the file specifying the query")
                ("shutdown-servers", "Shutdown the server components when client shuts down")
                ("test-id", po::value<std::string>(), "Test test ID")
                ("upload-iterations", po::value<int32_t>(), "Number of times the patient data file is uploaded")
                ("run-id", po::value<std::string>(), "Test run ID");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.contains("help"))
        {
            std::cout << desc << std::endl;
            exit(0);
        }

        if (vm.contains("plaintext"))
        {
            m_IsPlaintext = true;
        }

        if (vm.contains("random-ids"))
        {
            m_GenerateRandomIds = true;
        }

        if (vm.contains("patient-data-file"))
        {
            m_PatientDataFile = vm["patient-data-file"].as<std::string>();
        }

        if (vm.contains("attribute-config-file"))
        {
            m_AttributeConfigFile = vm["attribute-config-file"].as<std::string>();
        }

        if (vm.contains("query-file"))
        {
            m_QueryFile = vm["query-file"].as<std::string>();
        }

        if (vm.contains("shutdown-servers"))
        {
            m_ShutdownServers = true;
        }

        if (vm.contains("test-id"))
        {
            m_TestId = vm["test-id"].as<std::string>();
        }

        if (vm.contains("run-id"))
        {
            m_RunId = vm["run-id"].as<std::string>();
        }

        if (vm.contains("upload-iterations"))
        {
            m_UploadIterations = vm["upload-iterations"].as<int32_t>();
        }
    }

    bool CommandLineParameters::IsPlaintext() const
    {
        return m_IsPlaintext;
    }

    bool CommandLineParameters::IsGenerateRandomIds() const
    {
        return m_GenerateRandomIds;
    }

    bool CommandLineParameters::IsShutdownServers() const
    {
        return m_ShutdownServers;
    }

    bool CommandLineParameters::HasPatientDataFile() const
    {
        return m_PatientDataFile.has_value();
    }

    const std::string &CommandLineParameters::GetPatientDataFile() const
    {
        return m_PatientDataFile.value();
    }

    const std::string &CommandLineParameters::GetAttributeConfigFile() const
    {
        return m_AttributeConfigFile;
    }

    const std::string &CommandLineParameters::GetQueryFile() const
    {
        return m_QueryFile;
    }

    const std::string &CommandLineParameters::GetTestId() const
    {
        return m_TestId;
    }


    const std::string &CommandLineParameters::GetRunId() const
    {
        return m_RunId;
    }

    int32_t CommandLineParameters::GetUploadIterations() const
    {
        return m_UploadIterations;
    }
}
