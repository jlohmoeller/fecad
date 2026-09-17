#include "pdshared/grpc/Chunking.hpp"

namespace pat_disc {
    static constexpr int32_t kMaxBufferSize = 256 * 1024;

    void WriteChunkedFromStream(grpc::internal::WriterInterface<proto::ChunkedBytePayload> *writer, std::istream &stream)
    {
        proto::ChunkedBytePayload msg;
        std::string *payload = msg.mutable_payload();
        payload->resize(kMaxBufferSize);
        while (stream.good())
        {
            stream.read(payload->data(), kMaxBufferSize);
            const std::streamsize readBytes = stream.gcount();

            if (readBytes < kMaxBufferSize)
                payload->resize(readBytes);

            writer->Write(msg);

            if (readBytes < kMaxBufferSize)
                break;
        }
    }

    void ReadChunkedToStream(grpc::internal::ReaderInterface<proto::ChunkedBytePayload> *reader, std::ostream &stream)
    {
        proto::ChunkedBytePayload msg;
        while (reader->Read(&msg))
        {
            stream.write(msg.payload().data(), static_cast<std::streamsize>(msg.payload().size()));
        }
    }

    void WriteChunkedFromMessage(grpc::internal::WriterInterface<proto::ChunkedBytePayload> *writer, const google::protobuf::Message &msg)
    {
        std::stringstream ss;
        msg.SerializeToOstream(&ss);

        WriteChunkedFromStream(writer, ss);
    }

    void ReadChunkedToMessage(grpc::internal::ReaderInterface<proto::ChunkedBytePayload> *reader, google::protobuf::Message *msg)
    {
        std::stringstream ss;
        ReadChunkedToStream(reader, ss);

        msg->ParseFromIstream(&ss);
    }
}
