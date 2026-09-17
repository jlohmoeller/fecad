#include "pdshared/measure/ResultWriter.hpp"

#include <pdshared/measure/CommunicationCost.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Utilities.hpp>

namespace pat_disc {
    void ResultWriter::WriteResults(const ResultWriterInfo &info)
    {
        const auto &timings = Timer::GetInstance().GetResults();
        const auto &communicationCosts = CommunicationCost::GetInstance().GetResults();

        std::filesystem::path p = std::filesystem::current_path() / "test-results" / info.entity / info.testId;

        const auto t = std::time(nullptr);
        const auto tm = std::localtime(&t);

        std::ostringstream oss;
        oss << std::put_time(tm, "%d-%m-%Y %H-%M-%S");

        std::string fileName = std::format("{}.json", info.runId);
        std::filesystem::path filePath = p / fileName;
        create_directories(p);

        nlohmann::json root;
        root["time"] = oss.str();
        root["branch"] = ExecuteShellCommand("git rev-parse --abbrev-ref HEAD | tr -d '\n'");
        root["commit"] = ExecuteShellCommand("git rev-parse HEAD | tr -d '\n'");

        root["testId"] = info.testId;
        root["runId"] = info.runId;
        root["plaintext"] = info.plaintext;
        root["maxRamUsage"] = info.maxRamUsage;

        if (!info.attributeConfig.empty())
        {
            root["attributeConfig"] = info.attributeConfig;
        }

        if (!info.query.empty())
        {
            root["query"] = info.query;
        }

        if (!info.patientDataHash.empty())
        {
            root["patientDataHash"] = info.patientDataHash;
        }

        if (info.patientCount.has_value())
        {
            root["patientCount"] = info.patientCount.value();
        }

        if (info.storageSize.has_value())
        {
            root["storageSize"] = info.storageSize.value();
        }

        if (info.toyParameters.has_value())
        {
            root["toyParameters"] = info.toyParameters.value();
        }

        for (const auto &[label, time]: timings)
        {
            if (label != "Idle")
            {
                root["timings"][label] = time;
            }
        }

        for (const auto &[label, cost]: communicationCosts)
        {
            root["communicationCost"][label] = cost;
        }

        std::ofstream fs(filePath);
        fs << root;
    }
}
