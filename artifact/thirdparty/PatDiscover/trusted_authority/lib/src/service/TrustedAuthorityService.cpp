#include "pdta/service/TrustedAuthorityService.hpp"

#include <pdshared/grpc/Chunking.hpp>
#include <pdshared/measure/ResultWriter.hpp>
#include <pdshared/measure/Timer.hpp>
#include <pdshared/util/Utilities.hpp>

#include <pdta/config/CommandLineParameters.hpp>
#include <pdta/entity/TrustedAuthority.hpp>

namespace pat_disc {
    grpc::Status TrustedAuthorityService::GetCryptoContext(grpc::ServerContext *context, const proto::TaRequest *request,
                                                           grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received crypto context request");

        const auto type = static_cast<AttributeType>(request->type());
        std::ifstream fs = TrustedAuthority::GetCryptoContext(type);

        WriteChunkedFromStream(writer, fs);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetPublicKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                       grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received public key request");

        const auto type = static_cast<AttributeType>(request->type());
        std::ifstream fs = TrustedAuthority::GetPublicKey(type);

        WriteChunkedFromStream(writer, fs);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetMultKey(grpc::ServerContext *context, const proto::TaRequest *request, grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received mult key request");
        const auto type = static_cast<AttributeType>(request->type());

        std::ifstream fs = TrustedAuthority::GetMultKey(type);
        WriteChunkedFromStream(writer, fs);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetKeySwitchingKey(grpc::ServerContext *context,
                                                             grpc::ServerReaderWriter<proto::ChunkedBytePayload, proto::ChunkedBytePayload> *stream)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received key switching key request");

        proto::KeySwitchingKeyRequest req;
        ReadChunkedToMessage(stream, &req);

        std::stringstream resultStream = TrustedAuthority::CreateKeySwitchingKey(static_cast<AttributeType>(req.type()), req.public_key());
        WriteChunkedFromStream(stream, resultStream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetSchemeSwitchingKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                                grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received scheme switching key request");

        std::ifstream stream = TrustedAuthority::GetSchemeSwitchingKey(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetAutomorphismKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                             grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received automorphism key request");

        std::ifstream stream = TrustedAuthority::GetAutomorphismKey(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetBinFheCryptoContext(grpc::ServerContext *context, const proto::TaRequest *request,
                                                                 grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received BinFHE crypto context request");

        std::ifstream stream = TrustedAuthority::GetBinFheCryptoContext(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetBinFheRefreshKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                              grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received BinFHE refresh key request");

        std::ifstream stream = TrustedAuthority::GetBinFheRefreshKey(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetBinFheSwitchKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                             grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received BinFHE switch key request");

        std::ifstream stream = TrustedAuthority::GetBinFheSwitchKey(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

    grpc::Status TrustedAuthorityService::GetBinFheBootstrappingKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                                    grpc::ServerWriter<proto::BootstrappingKeyChunked> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received BinFHE bootstrapping key request");

        const auto map = TrustedAuthority::GetBinFheBTKey(static_cast<AttributeType>(request->type()));
        for (auto &[index, keys]: *map)
        {
            WriteBtKeyChunked(writer, keys.first, index, proto::KeyType::BS_KEY);
            WriteBtKeyChunked(writer, keys.second, index, proto::KeyType::KS_KEY);
        }

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }

#if PD_EVAL
    grpc::Status TrustedAuthorityService::Kill(grpc::ServerContext *context, const google::protobuf::Empty *request, grpc::ServerWriter<google::protobuf::Empty> *writer)
    {
        PD_TRACE("Received kill request");

        Timer::GetInstance().Shutdown();

        ResultWriterInfo info;
        info.entity = "TA";
        info.testId = ta::CommandLineParameters::GetInstance().GetTestId();
        info.runId = ta::CommandLineParameters::GetInstance().GetRunId();
        info.toyParameters = ta::CommandLineParameters::GetInstance().IsUseToyParameters();
        info.maxRamUsage = pat_disc::GetMaxRamUsageOfProcess();

        uintmax_t storageSize = 0;
        for (const auto &item: std::filesystem::recursive_directory_iterator("ta-storage"))
        {
            if (item.is_regular_file())
            {
                storageSize += file_size(item.path());
            }
        }

        info.storageSize = storageSize;

        ResultWriter::WriteResults(info);
        Logger::Shutdown();
        exit(0);
    }
#endif

#if PD_DEBUG
    grpc::Status TrustedAuthorityService::GetPrivateKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                                        grpc::ServerWriter<proto::ChunkedBytePayload> *writer)
    {
        Timer::GetInstance().Switch("Context Serving");

        PD_TRACE("Received private key request");

        std::ifstream stream = TrustedAuthority::GetPrivateKey(static_cast<AttributeType>(request->type()));
        WriteChunkedFromStream(writer, stream);

        Timer::GetInstance().Switch("Idle");

        return grpc::Status::OK;
    }
#endif

    void TrustedAuthorityService::WriteBtKeyChunked(grpc::ServerWriter<proto::BootstrappingKeyChunked> *writer, std::ifstream &fs, const uint32_t index,
                                                    proto::KeyType keyType)
    {
        constexpr int32_t kMaxBufferSize = 256 * 1024;

        proto::BootstrappingKeyChunked msg;
        msg.set_index(index);
        msg.set_type(keyType);

        std::string *payload = msg.mutable_data();
        payload->resize(kMaxBufferSize);
        while (fs.good())
        {
            fs.read(payload->data(), kMaxBufferSize);
            const std::streamsize readBytes = fs.gcount();

            if (readBytes < kMaxBufferSize)
                payload->resize(readBytes);

            writer->Write(msg);

            if (readBytes < kMaxBufferSize)
                break;
        }
    }
}
