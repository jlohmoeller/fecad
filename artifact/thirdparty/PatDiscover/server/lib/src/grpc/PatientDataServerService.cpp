#include "pdserver/grpc/PatientDataServerService.hpp"

#include <pdshared/grpc/Chunking.hpp>
#include <pdshared/measure/CommunicationCost.hpp>
#include <pdshared/measure/ResultWriter.hpp>
#include <pdshared/measure/Timer.hpp>

#include <pdserver/config/CommandLineParameters.hpp>
#include <pdserver/data/DatabaseWrapper.hpp>
#include <pdserver/entity/Server.hpp>
#include <pdshared/util/Utilities.hpp>

namespace pat_disc {
    struct InsertInformation
    {
        bool identifierPhase = false;
        int64_t insertCount = 0;

        std::string currentAttribute;
        sqlite3_stmt *preparedStatement = nullptr;
        std::unordered_map<std::string, int64_t> nextInsertIds;
    };

    grpc::Status PatientDataServerService::UploadPatientData(grpc::ServerContext *context, grpc::ServerReader<proto::PatientDataUpload> *reader,
                                                             google::protobuf::Empty *response)
    {
        Timer::GetInstance().Switch("Patient Data Upload");
        PD_INFO("Received patient data upload request");

        InsertInformation insertInfo;

        DatabaseWrapper::GetInstance().InitializeAttributeIdMap(insertInfo.nextInsertIds);
        DatabaseWrapper::GetInstance().StartTransaction();

        proto::PatientDataUpload data;
        while (reader->Read(&data))
        {
            if (data.has_id_data())
            {
                if (!insertInfo.identifierPhase)
                {
                    insertInfo.identifierPhase = true;
                    insertInfo.currentAttribute.clear();

                    insertInfo.preparedStatement = DatabaseWrapper::GetInstance().StartInsertPatientData();
                }

                for (const auto& id : data.id_data().ids())
                {
                    DatabaseWrapper::GetInstance().InsertPatientData(insertInfo.preparedStatement, data.id_data().provider(), id, insertInfo.insertCount);
                    DatabaseWrapper::GetInstance().ResetStatement(insertInfo.preparedStatement);

                    insertInfo.insertCount++;
                }
            } else if (data.has_attribute_data())
            {
                const std::string& attr = data.attribute_data().attribute();
                if (insertInfo.currentAttribute != attr)
                {
                    insertInfo.identifierPhase = false;
                    insertInfo.currentAttribute = attr;

                    insertInfo.preparedStatement = DatabaseWrapper::GetInstance().StartInsertCiphertext(attr);
                }

                DatabaseWrapper::GetInstance().InsertCiphertext(insertInfo.preparedStatement, insertInfo.nextInsertIds[attr], data.attribute_data().ciphertext());
                DatabaseWrapper::GetInstance().ResetStatement(insertInfo.preparedStatement);
                insertInfo.nextInsertIds[attr]++;
            }
        }

        DatabaseWrapper::GetInstance().CommitTransaction();
        DatabaseWrapper::GetInstance().UpdateNextIdInfo(insertInfo.insertCount);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status PatientDataServerService::ProcessQuery(grpc::ServerContext *context,
                                                        grpc::ServerReaderWriter<proto::QueryResponse, proto::ChunkedBytePayload> *stream)
    {
        Timer::GetInstance().Switch("Query Processing");
        PD_INFO("Received process query request");

        proto::QueryRequest req;
        ReadChunkedToMessage(stream, &req);
        try
        {
            CommunicationCost::GetInstance().SwitchLabel("Query Results");
            Server::GetInstance().ProcessQuery(req, stream);
        } catch (lbcrypto::OpenFHEException &e)
        {
            PD_ERROR("OpenFHE exception occurred: {}", e.what());
        }

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

#if PD_EVAL
    grpc::Status PatientDataServerService::Kill(grpc::ServerContext *context, const google::protobuf::Empty *request, grpc::ServerWriter<google::protobuf::Empty> *writer)
    {
        PD_TRACE("Received kill request");

        Timer::GetInstance().Shutdown();
        DatabaseWrapper::GetInstance().Close();

        ResultWriterInfo info;
        info.entity = "Server";
        info.testId = server::CommandLineParameters::GetInstance().GetTestId();
        info.runId = server::CommandLineParameters::GetInstance().GetRunId();
        info.attributeConfig = server::CommandLineParameters::GetInstance().GetAttributeConfigFile();
        info.plaintext = server::CommandLineParameters::GetInstance().IsPlaintext();
        info.storageSize = std::filesystem::file_size(server::CommandLineParameters::GetInstance().GetDatabaseFile());
        info.maxRamUsage = pat_disc::GetMaxRamUsageOfProcess();

        ResultWriter::WriteResults(info);
        Logger::Shutdown();

        exit(0);
    }
#endif
}
