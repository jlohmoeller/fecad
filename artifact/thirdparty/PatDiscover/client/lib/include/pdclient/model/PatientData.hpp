#ifndef SERVER_PATIENTDATA_HPP
#define SERVER_PATIENTDATA_HPP

namespace pat_disc {
    struct PatientData
    {
        std::vector<std::string> ids;
        std::map<std::string, std::vector<std::vector<std::variant<int64_t, double, Position<int64_t>, Position<double> > > > > data;
    };
}

#endif //SERVER_PATIENTDATA_HPP
