#ifndef TRUSTEDAUTHORITYSERVICE_HPP
#define TRUSTEDAUTHORITYSERVICE_HPP

#include <pdproto/services.grpc.pb.h>

namespace pat_disc {
    class TrustedAuthorityService final : public proto::TrustedAuthority::Service
    {
    public:
        grpc::Status GetCryptoContext(grpc::ServerContext *context, const proto::TaRequest *request,
                                      grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetPublicKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                  grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetMultKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetKeySwitchingKey(grpc::ServerContext *context,
                                        grpc::ServerReaderWriter<proto::ChunkedBytePayload, proto::ChunkedBytePayload> *stream) override;

        grpc::Status GetSchemeSwitchingKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                           grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetAutomorphismKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                        grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetBinFheCryptoContext(grpc::ServerContext *context, const proto::TaRequest *request,
                                            grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetBinFheRefreshKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                         grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetBinFheSwitchKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                        grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;

        grpc::Status GetBinFheBootstrappingKey(grpc::ServerContext *context, const proto::TaRequest *request,
                                               grpc::ServerWriter<proto::BootstrappingKeyChunked> *writer) override;

#if PD_EVAL
        grpc::Status Kill(grpc::ServerContext *context, const google::protobuf::Empty *request, grpc::ServerWriter<google::protobuf::Empty> *writer) override;
#endif

#if PD_DEBUG
        grpc::Status GetPrivateKey(grpc::ServerContext *context, const proto::TaRequest *request, grpc::ServerWriter<proto::ChunkedBytePayload> *writer) override;
#endif

    private:
        static void WriteBtKeyChunked(grpc::ServerWriter<proto::BootstrappingKeyChunked> *writer, std::ifstream &fs, uint32_t index, proto::KeyType keyType);
    };
}

#endif //TRUSTEDAUTHORITYSERVICE_HPP
