#include "pdshared/util/Serialization.hpp"

#include <pdproto/data_transfer_objects.pb.h>

namespace pat_disc {
    std::string SerializeToString(const Ciphertext &ct)
    {
        std::ostringstream ss;
        lbcrypto::Serial::Serialize(ct, ss, lbcrypto::SerType::BINARY);

        return ss.str();
    }

    Ciphertext DeserializeFromString(const std::string &data)
    {
        Ciphertext result;
        std::istringstream ss(data);
        lbcrypto::Serial::Deserialize(result, ss, lbcrypto::SerType::BINARY);

        return result;
    }

    std::string SerializePlainIntToString(const std::vector<int64_t> &data)
    {
        proto::IntegerPlaintext pt;
        for (const auto &value: data)
        {
            pt.add_values(value);
        }

        return pt.SerializeAsString();
    }

    IntegerEncoding DeserializePlainIntFromString(const std::string &data)
    {
        proto::IntegerPlaintext pt;
        pt.ParseFromString(data);

        return DeserializePlainIntFromMessage(pt);
    }

    IntegerEncoding DeserializePlainIntFromMessage(const proto::IntegerPlaintext &msg)
    {
        auto result = std::make_shared<std::vector<int64_t> >();
        for (const auto &value: msg.values())
        {
            result->push_back(value);
        }

        return result;
    }
}
