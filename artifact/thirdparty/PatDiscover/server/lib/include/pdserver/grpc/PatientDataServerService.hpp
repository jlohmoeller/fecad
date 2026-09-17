#ifndef PATIENTDATASERVERSERVICE_HPP
#define PATIENTDATASERVERSERVICE_HPP

#include <pdproto/services.grpc.pb.h>

namespace pat_disc {
    class PatientDataServerService final : public proto::PatientDataServer::Service
    {
    public:
        grpc::Status UploadPatientData(grpc::ServerContext *context, grpc::ServerReader<proto::PatientDataUpload> *reader,
                                       google::protobuf::Empty *response) override;

        grpc::Status ProcessQuery(grpc::ServerContext *context,
                                  grpc::ServerReaderWriter<proto::QueryResponse, proto::ChunkedBytePayload> *stream) override;

#if PD_EVAL
        grpc::Status Kill(grpc::ServerContext *context, const google::protobuf::Empty *request, grpc::ServerWriter<google::protobuf::Empty> *writer) override;
#endif
    };
}


#endif //PATIENTDATASERVERSERVICE_HPP
