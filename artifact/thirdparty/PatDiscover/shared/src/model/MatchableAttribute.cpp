#include "pdshared/model/MatchableAttribute.hpp"

#include "pdshared/config/Parameters.hpp"

namespace pat_disc {
    const std::map<AttributeType, std::string> kAttributeTypeNames = {
        {AttributeType::BOOLEAN, "Boolean"},
        {AttributeType::ENUM_PRECISE, "EnumPrecise"},
        {AttributeType::ENUM_APPROX, "EnumApprox"},
        {AttributeType::CONTINUOUS_PRECISE, "ContinuousPrecise"},
        {AttributeType::CONTINUOUS_APPROX, "ContinuousApprox"},
        {AttributeType::DISTANCE_PRECISE, "DistancePrecise"},
        {AttributeType::DISTANCE_APPROX, "DistanceApprox"}
    };

    const std::map<std::string, AttributeType> kNameToAttributeType = {
        {"Boolean", AttributeType::BOOLEAN},
        {"EnumPrecise", AttributeType::ENUM_PRECISE},
        {"EnumApprox", AttributeType::ENUM_APPROX},
        {"ContinuousPrecise", AttributeType::CONTINUOUS_PRECISE},
        {"ContinuousApprox", AttributeType::CONTINUOUS_APPROX},
        {"DistancePrecise", AttributeType::DISTANCE_PRECISE},
        {"DistanceApprox", AttributeType::DISTANCE_APPROX}
    };

    std::map<std::string, std::shared_ptr<MatchableAttribute> > MatchableAttribute::s_PatientAttributes = {};

    std::ostream& operator<<(std::ostream& os, const AttributeType &attr)
    {
        switch (attr)
        {
            case AttributeType::BOOLEAN:
                os << "Boolean";
                break;
            case AttributeType::ENUM_PRECISE:
                os << "EnumPrecise";
                break;
            case AttributeType::ENUM_APPROX:
                os << "EnumApprox";
                break;
            case AttributeType::CONTINUOUS_PRECISE:
                os << "ContinuousPrecise";
                break;
            case AttributeType::CONTINUOUS_APPROX:
                os << "ContinuousApprox";
                break;
            case AttributeType::DISTANCE_PRECISE:
                os << "DistancePrecise";
                break;
            case AttributeType::DISTANCE_APPROX:
                os << "DistanceApprox";
                break;
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Invalid attribute type");
                break;
        }

        return os;
    }

    MatchableAttribute::MatchableAttribute(const std::string &attr) : m_Attribute(attr)
    {
    }

    uint32_t MatchableAttribute::GetAttributeCountForType(const AttributeType type)
    {
        uint32_t result = 0;

        for (const auto &attr: s_PatientAttributes | std::views::values)
        {
            if (attr->GetAttributeType() == type)
            {
                result++;
            }
        }

        return result;
    }

    uint32_t MatchableAttribute::GetEffectiveAttributeCountForType(const AttributeType type)
    {
        return std::bit_ceil(GetAttributeCountForType(type));
    }

    uint32_t MatchableAttribute::GetBatchSizeForType(const AttributeType type)
    {
        switch (type)
        {
            case AttributeType::BOOLEAN:
            case AttributeType::ENUM_PRECISE:
                return k_BFVBatchSize;
            case AttributeType::ENUM_APPROX:
            case AttributeType::CONTINUOUS_APPROX:
                return k_CKKSBatchSize;
            case AttributeType::CONTINUOUS_PRECISE:
            case AttributeType::DISTANCE_PRECISE:
                return k_BFVBatchSize;
            case AttributeType::DISTANCE_APPROX:
                return k_CKKSBatchSize;
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Invalid attribute type")
        }

        PD_ASSERT(false, "Unknown attribute type")
        return 0;
    }

    AttributeType MatchableAttribute::GetAttributeType(const std::string &attr)
    {
        return s_PatientAttributes.at(attr)->GetAttributeType();
    }

    void MatchableAttribute::Parse(const std::string &filePath)
    {
        std::ifstream f(filePath, std::ifstream::binary);

        if (f.is_open())
        {
            nlohmann::json jsonData = nlohmann::json::parse(f);

            for (const auto &item: jsonData)
            {
                std::string attr = item.at("name");
                const AttributeType type = kNameToAttributeType.at(item.at("type"));

                switch (type)
                {
                    case AttributeType::BOOLEAN:
                        s_PatientAttributes.insert({attr, std::make_shared<BooleanAttribute>(attr)});
                        break;
                    case AttributeType::ENUM_PRECISE:
                        s_PatientAttributes.insert({attr, std::make_shared<PreciseEnumAttribute>(attr)});
                        break;
                    case AttributeType::ENUM_APPROX:
                    {
                        int64_t minValue = item.at("minValue");
                        int64_t maxValue = item.at("maxValue");
                        s_PatientAttributes.insert({attr, std::make_shared<ApproxEnumAttribute>(attr, minValue, maxValue)});
                    }
                    break;
                    case AttributeType::CONTINUOUS_PRECISE:
                        s_PatientAttributes.insert({attr, std::make_shared<PreciseContinuousAttribute>(attr, item.at("decimalPrecision"))});
                        break;
                    case AttributeType::CONTINUOUS_APPROX:
                    {
                        double minValue = item.at("minValue");
                        double maxValue = item.at("maxValue");
                        int32_t decimalPrecision = item.at("decimalPrecision");

                        s_PatientAttributes.insert({attr, std::make_shared<ApproxContinuousAttribute>(attr, minValue, maxValue, decimalPrecision)});
                    }
                    break;
                    case AttributeType::DISTANCE_PRECISE:
                        s_PatientAttributes.insert({attr, std::make_shared<PreciseDistanceAttribute>(attr, item.at("decimalPrecision"))});
                        break;
                    case AttributeType::DISTANCE_APPROX:
                    {
                        double xMin = item.at("xMinValue");
                        double xMax = item.at("xMaxValue");
                        double yMin = item.at("yMinValue");
                        double yMax = item.at("yMaxValue");
                        double zMin = item.at("zMinValue");
                        double zMax = item.at("zMaxValue");
                        int32_t decimalPrecision = item.at("decimalPrecision");

                        s_PatientAttributes.insert({attr, std::make_shared<ApproxDistanceAttribute>(attr, xMin, xMax, yMin, yMax, zMin, zMax, decimalPrecision)});
                    }
                    break;
                    case AttributeType::TYPE_COUNT:
                        PD_ASSERT(false, "Use of invalid attribute type")
                        break;
                }
            }
        } else
        {
            PD_ASSERT(false, "Could not open attribute configuration file")
        }
    }

    const std::map<std::string, std::shared_ptr<MatchableAttribute> > &MatchableAttribute::GetAttributes()
    {
        return s_PatientAttributes;
    }


    BooleanAttribute::BooleanAttribute(const std::string &attr) : MatchableAttribute(attr)
    {
    }

    BooleanAttribute &BooleanAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::BOOLEAN, "Called get on incorrect attribute")

        return dynamic_cast<BooleanAttribute &>(*ma);
    }

    AttributeType BooleanAttribute::GetAttributeType()
    {
        return AttributeType::BOOLEAN;
    }

    PreciseEnumAttribute::PreciseEnumAttribute(const std::string &attr) : MatchableAttribute(attr)
    {
    }

    AttributeType PreciseEnumAttribute::GetAttributeType()
    {
        return AttributeType::ENUM_PRECISE;
    }

    PreciseEnumAttribute &PreciseEnumAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::ENUM_PRECISE, "Called get on incorrect attribute")

        return dynamic_cast<PreciseEnumAttribute &>(*ma);
    }

    ApproxEnumAttribute::ApproxEnumAttribute(const std::string &attr, const int32_t minValue, const int32_t maxValue)
        : MatchableAttribute(attr),
          m_MinValue(minValue),
          m_MaxValue(maxValue)
    {
    }

    AttributeType ApproxEnumAttribute::GetAttributeType()
    {
        return AttributeType::ENUM_APPROX;
    }

    double ApproxEnumAttribute::ConvertValue(const int64_t value) const
    {
        return static_cast<double>(value - m_MinValue) / static_cast<double>(m_MaxValue - m_MinValue);
    }

    ApproxEnumAttribute &ApproxEnumAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::ENUM_APPROX, "Called get on incorrect attribute")

        return dynamic_cast<ApproxEnumAttribute &>(*ma);
    }

    PreciseContinuousAttribute::PreciseContinuousAttribute(const std::string &attr, int32_t decimalPrecision) : MatchableAttribute(attr),
                                                                                                                m_DecimalPrecision(decimalPrecision)
    {
    }

    PreciseContinuousAttribute &PreciseContinuousAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::CONTINUOUS_PRECISE, "Called get on incorrect attribute")

        return dynamic_cast<PreciseContinuousAttribute &>(*ma);
    }

    AttributeType PreciseContinuousAttribute::GetAttributeType()
    {
        return AttributeType::CONTINUOUS_PRECISE;
    }

    int64_t PreciseContinuousAttribute::ConvertValue(const double value) const
    {
        return static_cast<int64_t>(std::round(value * std::pow(10, m_DecimalPrecision)));
    }

    ApproxContinuousAttribute::ApproxContinuousAttribute(const std::string &attr, const double minValue, const double maxValue, const int32_t decimalPrecision)
        : MatchableAttribute(attr), m_MinValue(minValue), m_MaxValue(maxValue), m_DecimalPrecision(decimalPrecision)
    {
    }

    AttributeType ApproxContinuousAttribute::GetAttributeType()
    {
        return AttributeType::CONTINUOUS_APPROX;
    }

    double ApproxContinuousAttribute::ConvertValue(const double value) const
    {
        return (value - m_MinValue) / (m_MaxValue - m_MinValue) * k_ContinuousApproxMaxValue;
    }

    int64_t ApproxContinuousAttribute::ConvertPlaintextValue(const double value) const
    {
        return static_cast<int64_t>(std::round(value * std::pow(10, m_DecimalPrecision)));
    }

    ApproxContinuousAttribute &ApproxContinuousAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::CONTINUOUS_APPROX, "Called get on incorrect attribute")

        return dynamic_cast<ApproxContinuousAttribute &>(*ma);
    }

    PreciseDistanceAttribute::PreciseDistanceAttribute(const std::string &attr, const int32_t decimalPrecision) : MatchableAttribute(attr),
        m_DecimalPrecision(decimalPrecision)
    {
    }

    PreciseDistanceAttribute &PreciseDistanceAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::DISTANCE_PRECISE, "Called get on incorrect attribute")

        return dynamic_cast<PreciseDistanceAttribute &>(*ma);
    }

    AttributeType PreciseDistanceAttribute::GetAttributeType()
    {
        return AttributeType::DISTANCE_PRECISE;
    }

    Position<int64_t> PreciseDistanceAttribute::ConvertPosition(const Position<double> &pos) const
    {
        Position<int64_t> result{};
        result.x = ConvertDataValue(pos.x);
        result.y = ConvertDataValue(pos.y);
        result.z = ConvertDataValue(pos.z);

        return result;
    }

    int64_t PreciseDistanceAttribute::ConvertDataValue(const double value) const
    {
        return static_cast<int64_t>(std::round(value * std::pow(10, m_DecimalPrecision)));
    }

    int64_t PreciseDistanceAttribute::ConvertBoundValue(const double value) const
    {
        return static_cast<int64_t>(std::round(std::pow(value, 2) * std::pow(10, 2 * m_DecimalPrecision)));
    }

    ApproxDistanceAttribute::ApproxDistanceAttribute(const std::string &attr, const double xMin, const double xMax, const double yMin,
                                                     const double yMax, const double zMin, const double zMax, const int32_t decimalPrecision)
        : MatchableAttribute(attr), m_MinX(xMin), m_MinY(yMin), m_MinZ(zMin), m_MaxX(xMax), m_MaxY(yMax), m_MaxZ(zMax), m_DecimalPrecision(decimalPrecision)
    {
    }

    ApproxDistanceAttribute &ApproxDistanceAttribute::Get(const std::string &attr)
    {
        const std::shared_ptr<MatchableAttribute> &ma = s_PatientAttributes.at(attr);
        const AttributeType type = ma->GetAttributeType();

        PD_ASSERT(type == AttributeType::DISTANCE_APPROX, "Called get on incorrect attribute")

        return dynamic_cast<ApproxDistanceAttribute &>(*ma);
    }

    AttributeType ApproxDistanceAttribute::GetAttributeType()
    {
        return AttributeType::DISTANCE_APPROX;
    }

    Position<double> ApproxDistanceAttribute::ConvertValue(const Position<double> &pos) const
    {
        const double maxValue = std::sqrt(k_DistanceApproxMaxValue / 3.0);

        Position<double> result{};
        result.x = (pos.x - m_MinX) / (m_MaxX - m_MinX) * maxValue;
        result.y = (pos.y - m_MinY) / (m_MaxY - m_MinY) * maxValue;
        result.z = (pos.z - m_MinZ) / (m_MaxZ - m_MinZ) * maxValue;

        return result;
    }

    Position<int64_t> ApproxDistanceAttribute::ConvertPlaintextValue(const Position<double> &pos) const
    {
        Position<int64_t> result{};
        result.x = static_cast<int64_t>(std::round(pos.x * std::pow(10, m_DecimalPrecision)));
        result.y = static_cast<int64_t>(std::round(pos.y * std::pow(10, m_DecimalPrecision)));
        result.z = static_cast<int64_t>(std::round(pos.z * std::pow(10, m_DecimalPrecision)));

        return result;
    }

    double ApproxDistanceAttribute::ConvertUpperBoundValue(const double upperBound) const
    {
        const double xMaxDiff = std::pow(m_MaxX - m_MinX, 2);
        const double yMaxDiff = std::pow(m_MaxY - m_MinY, 2);
        const double zMaxDiff = std::pow(m_MaxZ - m_MinZ, 2);
        const double maxDistance = xMaxDiff + yMaxDiff + zMaxDiff;

        return std::pow(upperBound, 2) / maxDistance * k_DistanceApproxMaxValue;
    }

    int64_t ApproxDistanceAttribute::ConvertPlaintextUpperBoundValue(const double upperBound) const
    {
        return static_cast<int64_t>(std::round(std::pow(upperBound, 2) * std::pow(10, 2 * m_DecimalPrecision)));
    }
}
