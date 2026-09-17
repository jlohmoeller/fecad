#include "pdclient/grpc/PatientDataServerClient.hpp"

#include <pdclient/processing/ClientQueryProcessing.hpp>
#include <pdshared/grpc/Chunking.hpp>
#include <pdshared/measure/CommunicationCost.hpp>
#include <pdshared/measure/Timer.hpp>

namespace pat_disc {
    PatientDataServerClient::PatientDataServerClient(const std::shared_ptr<grpc::Channel> &channel)
        : m_Stub(proto::PatientDataServer::NewStub(channel))
    {
    }

    void PatientDataServerClient::StartDataUpload()
    {
        CommunicationCost::GetInstance().SwitchLabel("Data Upload");
        m_UploadWriter = m_Stub->UploadPatientData(&m_ClientContext, &m_UploadResponse);
    }

    void PatientDataServerClient::UploadPatientData(const proto::PatientDataUpload &data) const
    {
        m_UploadWriter->Write(data);
    }

    void PatientDataServerClient::EndDataUpload()
    {
        m_UploadWriter->WritesDone();
        const grpc::Status status = m_UploadWriter->Finish();
        if (!status.ok())
        {
            PD_ERROR("Error during upload patient data request: {}", status.error_message());
        }

        m_UploadWriter = nullptr;
    }

    void PatientDataServerClient::SendQuery(const proto::QueryRequest &data, const Client *client) const
    {
        CommunicationCost::GetInstance().SwitchLabel("Query Upload");

        grpc::ClientContext context;
        const auto readWrite = m_Stub->ProcessQuery(&context);
        WriteChunkedFromMessage(readWrite.get(), data);
        readWrite->WritesDone();

        Timer::GetInstance().Switch("Idle");
        ClientQueryProcessing proc(client, readWrite);
        Timer::GetInstance().Switch("Query Execution");

        auto status = readWrite->Finish();
        if (!status.ok())
        {
            PD_ERROR("Error during send query request: {}", status.error_message());
        }
    }

#if PD_EVAL
    void PatientDataServerClient::Kill() const
    {
        grpc::ClientContext context;
        const google::protobuf::Empty req;
        const auto reader = m_Stub->Kill(&context, req);
        reader->Finish();
    }
#endif
}
