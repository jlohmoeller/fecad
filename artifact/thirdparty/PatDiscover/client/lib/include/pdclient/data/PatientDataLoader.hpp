#ifndef SERVER_PATIENTDATALOADER_HPP
#define SERVER_PATIENTDATALOADER_HPP

#include "pdclient/model/PatientData.hpp"

namespace pat_disc {
    class PatientDataLoader
    {
    public:
        static PatientData LoadData(const std::string &jsonPath);

    private:
        static void ParseItem(const nlohmann::json &j, PatientData &p);

        static void ParseDataPoint(const std::string& jsonName, const nlohmann::json& j, PatientData &data);

    private:
        static int64_t s_CurrentId;
    };
}

#endif //SERVER_PATIENTDATALOADER_HPP
