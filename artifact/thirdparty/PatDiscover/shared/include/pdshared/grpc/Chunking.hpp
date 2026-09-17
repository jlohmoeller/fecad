#ifndef CHUNKING_HPP
#define CHUNKING_HPP

#include <pdproto/services.pb.h>

#include <google/protobuf/message.h>
#include <grpc++/support/sync_stream.h>

namespace pat_disc {
    void WriteChunkedFromStream(grpc::internal::WriterInterface<proto::ChunkedBytePayload> *writer, std::istream &stream);

    void ReadChunkedToStream(grpc::internal::ReaderInterface<proto::ChunkedBytePayload> *reader, std::ostream &stream);

    void WriteChunkedFromMessage(grpc::internal::WriterInterface<proto::ChunkedBytePayload> *writer, const google::protobuf::Message &msg);

    void ReadChunkedToMessage(grpc::internal::ReaderInterface<proto::ChunkedBytePayload> *reader, google::protobuf::Message *msg);
}

#endif //CHUNKING_HPP
