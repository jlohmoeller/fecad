#include "pdclient/data/PatientDataLoader.hpp"

#include <grpcpp/impl/codegen/config_protobuf.h>

#include "pdclient/config/CommandLineParameters.hpp"

namespace pat_disc {
    int64_t PatientDataLoader::s_CurrentId = 0;

    void PatientDataLoader::ParseDataPoint(const std::string &jsonName, const nlohmann::json &j, PatientData &data)
    {
        std::string attr = jsonName;
        attr[0] = static_cast<char>(toupper(static_cast<unsigned char>(attr[0])));

        // Did not add warning for performance reasons (endless printing)
        if (!MatchableAttribute::GetAttributes().contains(attr))
            return;

        if (!data.data.contains(attr))
        {
            data.data.insert({attr, {}});
        }

        const AttributeType type = MatchableAttribute::GetAttributeType(attr);
        const int64_t batchSize = MatchableAttribute::GetBatchSizeForType(type);

        if (data.data.at(attr).empty() || data.data.at(attr).back().size() == batchSize)
        {
            data.data.at(attr).emplace_back();
        }

        switch (type)
        {
            case AttributeType::BOOLEAN:
            case AttributeType::ENUM_PRECISE:
            {
                data.data.at(attr).back().emplace_back(static_cast<int64_t>(j.at(jsonName)));
            }
            break;
            case AttributeType::ENUM_APPROX:
            {
                const int64_t value = j.at(jsonName);

                if (client::CommandLineParameters::GetInstance().IsPlaintext())
                {
                    data.data.at(attr).back().emplace_back(value);
                } else
                {
                    double converted = ApproxEnumAttribute::Get(attr).ConvertValue(value);
                    data.data.at(attr).back().emplace_back(converted);
                }
            }
            break;
            case AttributeType::CONTINUOUS_PRECISE:
            {
                const double value = j.at(jsonName);
                const int64_t convertedValue = PreciseContinuousAttribute::Get(attr).ConvertValue(value);
                data.data.at(attr).back().emplace_back(convertedValue);
            }
            break;
            case AttributeType::CONTINUOUS_APPROX:
            {
                const double value = j.at(jsonName);

                if (client::CommandLineParameters::GetInstance().IsPlaintext())
                {
                    const int64_t converted = ApproxContinuousAttribute::Get(attr).ConvertPlaintextValue(value);
                    data.data.at(attr).back().emplace_back(converted);
                } else
                {
                    const double converted = ApproxContinuousAttribute::Get(attr).ConvertValue(value);
                    data.data.at(attr).back().emplace_back(converted);
                }
            }
            break;
            case AttributeType::DISTANCE_PRECISE:
            {
                const nlohmann::json &item = j.at(jsonName);
                Position converted = PreciseDistanceAttribute::Get(attr).ConvertPosition({item.at("x"), item.at("y"), item.at("z")});
                data.data.at(attr).back().emplace_back(converted);
            }
            break;
            case AttributeType::DISTANCE_APPROX:
            {
                const nlohmann::json &item = j.at(jsonName);

                if (client::CommandLineParameters::GetInstance().IsPlaintext())
                {
                    Position converted = ApproxDistanceAttribute::Get(attr).ConvertPlaintextValue({item.at("x"), item.at("y"), item.at("z")});
                    data.data.at(attr).back().emplace_back(converted);
                } else
                {
                    Position converted = ApproxDistanceAttribute::Get(attr).ConvertValue({item.at("x"), item.at("y"), item.at("z")});
                    data.data.at(attr).back().emplace_back(converted);
                }
            }
            break;
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Use of invalid attribute type")
                break;
        }
    }

    void PatientDataLoader::ParseItem(const nlohmann::json &j, PatientData &p)
    {
        if (client::CommandLineParameters::GetInstance().IsGenerateRandomIds())
        {
            p.ids.push_back(std::to_string(s_CurrentId));
            s_CurrentId++;
        } else
        {
            p.ids.push_back(j.at("id"));
        }

        for (auto it = j.begin(); it != j.end(); ++it)
        {
            ParseDataPoint(it.key(), j, p);
        }
    }

    PatientData PatientDataLoader::LoadData(const std::string &jsonPath)
    {
        PD_TRACE("Loading data from JSON file {}", jsonPath);

        if (std::ifstream f(jsonPath, std::ifstream::binary); f.is_open())
        {
            nlohmann::json jsonData = nlohmann::json::parse(f);

            PatientData result;
            for (const auto &data: jsonData)
            {
                ParseItem(data, result);
            }

            for (auto &[attr, data]: result.data)
            {
                const auto type = MatchableAttribute::GetAttributeType(attr);
                const int64_t batchSize = MatchableAttribute::GetBatchSizeForType(type);

                const auto lastValue = data.back().back();
                size_t currentSize = data.back().size();
                data.back().reserve(batchSize);


                for (int32_t i = 0; i < batchSize - currentSize; i++)
                {
                    data.back().push_back(lastValue);
                }
            }

            PD_TRACE("Finished data loading");

            return result;
        }

        PD_WARN("File {} could not be opened", jsonPath);
        return {};
    }
}
