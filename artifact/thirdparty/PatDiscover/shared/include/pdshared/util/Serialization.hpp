#ifndef SERIALIZATION_HPP
#define SERIALIZATION_HPP

#include <pdproto/data_transfer_objects.grpc.pb.h>

#include "pdshared/model/DataTransferObjects.hpp"

namespace pat_disc {
    std::string SerializeToString(const Ciphertext &ct);

    Ciphertext DeserializeFromString(const std::string &data);

    std::string SerializePlainIntToString(const std::vector<int64_t> &data);

    IntegerEncoding DeserializePlainIntFromString(const std::string &data);

    IntegerEncoding DeserializePlainIntFromMessage(const proto::IntegerPlaintext& msg);
}

#endif //SERIALIZATION_HPP
