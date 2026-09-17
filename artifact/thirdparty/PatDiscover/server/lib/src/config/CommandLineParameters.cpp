#ifndef COMMANDLINEPARAMETER_HPP
#define COMMANDLINEPARAMETER_HPP

#include "pdserver/config/CommandLineParameters.hpp"

#include <boost/program_options.hpp>

namespace pat_disc::server {
    CommandLineParameters *CommandLineParameters::s_Instance = new CommandLineParameters();

    CommandLineParameters &CommandLineParameters::GetInstance()
    {
        return *s_Instance;
    }

    CommandLineParameters::CommandLineParameters(): m_DatabaseFile("db/server.db")
    {
    }

    void CommandLineParameters::Parse(const int argc, char **argv)
    {
        namespace po = boost::program_options;

        po::options_description desc("Allowed Options");
        desc.add_options()
                ("help", "Produce help message")
                ("plaintext,P", "Perform computations in plaintext")
                ("drop-tables,D", "Drop the database tables on start")
                ("database-file", po::value<std::string>(), "Path to the database file for the patient data")
                ("attribute-config-file", po::value<std::string>(), "Path to the file configuring the attributes")
                ("test-id", po::value<std::string>(), "Test test ID")
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

        if (vm.contains("drop-tables"))
        {
            m_DropTablesOnStart = true;
        }

        if (vm.contains("database-file"))
        {
            m_DatabaseFile = vm["database-file"].as<std::string>();
        }

        if (vm.contains("attribute-config-file"))
        {
            m_AttributeConfigFile = vm["attribute-config-file"].as<std::string>();
        }

        if (vm.contains("test-id"))
        {
            m_TestId = vm["test-id"].as<std::string>();
        }

        if (vm.contains("run-id"))
        {
            m_RunId = vm["run-id"].as<std::string>();
        }
    }

    bool CommandLineParameters::IsPlaintext() const
    {
        return m_IsPlaintext;
    }

    bool CommandLineParameters::IsDropTablesOnStart() const
    {
        return m_DropTablesOnStart;
    }

    const std::string &CommandLineParameters::GetDatabaseFile() const
    {
        return m_DatabaseFile;
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

#endif //COMMANDLINEPARAMETER_HPP
