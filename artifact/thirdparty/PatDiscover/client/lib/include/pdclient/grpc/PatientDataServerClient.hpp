#ifndef PATIENTDATASERVERCLIENT_HPP
#define PATIENTDATASERVERCLIENT_HPP

#include <grpc++/channel.h>
#include <pdproto/services.grpc.pb.h>

namespace pat_disc {
    class Client;

    class PatientDataServerClient
    {
    public:
        explicit PatientDataServerClient(const std::shared_ptr<grpc::Channel> &channel);

        void StartDataUpload();

        void UploadPatientData(const proto::PatientDataUpload &data) const;

        void EndDataUpload();

        void SendQuery(const proto::QueryRequest &data, const Client *client) const;

#if PD_EVAL
        void Kill() const;
#endif

    private:
        std::unique_ptr<proto::PatientDataServer::Stub> m_Stub;

        grpc::ClientContext m_ClientContext;
        google::protobuf::Empty m_UploadResponse;

        std::unique_ptr<grpc::ClientWriter<proto::PatientDataUpload>> m_UploadWriter;
    };
}

#endif //PATIENTDATASERVERCLIENT_HPP
