#include "pdclient/data/QueryBuilder.hpp"

#include <pdshared/util/Serialization.hpp>

#include "pdclient/config/CommandLineParameters.hpp"

namespace pat_disc {
    const std::map<std::string, CombineOperator> kNameToCombineOperator = {
        {"Or", CombineOperator::OR},
        {"And", CombineOperator::AND},
        {"Sum", CombineOperator::SUM}
    };

    std::ostream& operator<<(std::ostream& os, const CombineOperator& o)
    {
        switch (o)
        {
            case CombineOperator::OR:
                os << "OR";
                break;
            case CombineOperator::AND:
                os << "AND";
                break;
            case CombineOperator::SUM:
                os << "SUM";
                break;
        }

        return os;
    }

    QueryAttributeData::QueryAttributeData(const std::string &attr, const bool invertResult)
        : m_Attribute(attr), m_InvertResult(invertResult)
    {
    }

    void QueryAttributeData::SetBooleanValue(int64_t value)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::BOOLEAN, "Attribute type is not BOOLEAN")
        PD_ASSERT(value == 0 || value == 1, "Value is not boolean")

        m_Data = value;
    }

    void QueryAttributeData::SetPreciseEnumValue(const int64_t value)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::ENUM_PRECISE, "Attribute type is not ENUM_PRECISE")

        m_Data = value;
    }

    void QueryAttributeData::SetApproxEnumValue(const int64_t value)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::ENUM_APPROX, "Attribute type is not ENUM_APPROX")

        if (client::CommandLineParameters::GetInstance().IsPlaintext())
            m_Data = value;
        else
            m_Data = ApproxEnumAttribute::Get(m_Attribute).ConvertValue(value);
    }

    void QueryAttributeData::SetPreciseContinuousValue(const double lb, const double ub)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::CONTINUOUS_PRECISE, "Attribute type is not CONTINUOUS_PRECISE")

        const int64_t lowerBound = PreciseContinuousAttribute::Get(m_Attribute).ConvertValue(lb);
        const int64_t upperBound = PreciseContinuousAttribute::Get(m_Attribute).ConvertValue(ub);

        m_Data = RangeQuery{lowerBound, upperBound};
    }

    void QueryAttributeData::SetApproxContinuousValue(const double lb, const double ub)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::CONTINUOUS_APPROX, "Attribute type is not CONTINUOUS_APPROX")

        const auto attr = ApproxContinuousAttribute::Get(m_Attribute);
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const int64_t lower = attr.ConvertPlaintextValue(lb);
            const int64_t upper = attr.ConvertPlaintextValue(ub);
            m_Data = RangeQuery{lower, upper};
        } else
        {
            const double lower = attr.ConvertValue(lb);
            const double upper = attr.ConvertValue(ub);
            m_Data = RangeQuery{lower, upper};
        }
    }

    void QueryAttributeData::SetPreciseDistanceValue(const double x, const double y, const double z, const double ub)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::DISTANCE_PRECISE, "Attribute type is not DISTANCE_PRECISE")
        const auto [convX, convY, convZ] = PreciseDistanceAttribute::Get(m_Attribute).ConvertPosition({x, y, z});
        const int64_t convertedBound = PreciseDistanceAttribute::Get(m_Attribute).ConvertBoundValue(ub);

        m_Data = PositionQuery{convX, convY, convZ, convertedBound};
    }

    void QueryAttributeData::SetApproxDistanceValue(const double x, const double y, const double z, const double ub)
    {
        PD_ASSERT(MatchableAttribute::GetAttributeType(m_Attribute) == AttributeType::DISTANCE_APPROX, "Attribute type is not DISTANCE_APPROX")
        const auto attr = ApproxDistanceAttribute::Get(m_Attribute);

        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            const auto [convX, convY, convZ] = attr.ConvertPlaintextValue({x, y, z});
            const int64_t convertedBound = attr.ConvertPlaintextUpperBoundValue(ub);
            m_Data = PositionQuery{convX, convY, convZ, convertedBound};
        } else
        {
            const auto [convX, convY, convZ] = attr.ConvertValue({x, y, z});
            const double convertedBound = attr.ConvertUpperBoundValue(ub);
            m_Data = PositionQuery{convX, convY, convZ, convertedBound};
        }
    }

    void QueryAttributeData::Parse(const nlohmann::json &attrData, const AttributeType attributeType)
    {
        switch (attributeType)
        {
            case AttributeType::BOOLEAN:
                SetBooleanValue(attrData.at("value"));
                break;
            case AttributeType::ENUM_PRECISE:
                SetPreciseEnumValue(attrData.at("value"));
                break;
            case AttributeType::ENUM_APPROX:
                SetApproxEnumValue(attrData.at("value"));
            break;
            case AttributeType::CONTINUOUS_PRECISE:
                SetPreciseContinuousValue(attrData.at("lowerBound"), attrData.at("upperBound"));
                break;
            case AttributeType::CONTINUOUS_APPROX:
                SetApproxContinuousValue(attrData.at("lowerBound"), attrData.at("upperBound"));
            break;
            case AttributeType::DISTANCE_PRECISE:
                SetPreciseDistanceValue(attrData.at("x"), attrData.at("y"), attrData.at("z"), attrData.at("upperBound"));
                break;
            case AttributeType::DISTANCE_APPROX:
                SetApproxDistanceValue(attrData.at("x"), attrData.at("y"), attrData.at("z"), attrData.at("upperBound"));
            break;
            case AttributeType::TYPE_COUNT:
                PD_ASSERT(false, "Use of invalid attribute type")
                break;
        }
    }

    void QueryAttributeData::Pack(proto::QueryAttributeData *attr, const Client &c) const
    {
        PD_ASSERT(!std::holds_alternative<std::monostate>(m_Data), "Attribute data is not set")
        attr->set_attribute(m_Attribute);
        attr->set_invert_result(m_InvertResult);

        const auto type = MatchableAttribute::GetAttributeType(m_Attribute);
        const int64_t batchSize = MatchableAttribute::GetBatchSizeForType(type);
        if (client::CommandLineParameters::GetInstance().IsPlaintext())
        {
            switch (type)
            {
                case AttributeType::BOOLEAN:
                case AttributeType::ENUM_PRECISE:
                case AttributeType::ENUM_APPROX:
                {
                    const int64_t value = std::get<int64_t>(m_Data);
                    proto::IntegerPlaintext *pt = attr->mutable_single_int_plain();

                    for (int32_t i = 0; i < batchSize; i++)
                    {
                        pt->add_values(value);
                    }
                }
                break;
                case AttributeType::CONTINUOUS_PRECISE:
                case AttributeType::CONTINUOUS_APPROX:
                {
                    const auto [lb, ub] = std::get<RangeQuery<int64_t> >(m_Data);
                    proto::RangeQuery *range = attr->mutable_range();
                    proto::IntegerPlaintext *lbPt = range->mutable_plain_lower();
                    proto::IntegerPlaintext *ubPt = range->mutable_plain_upper();

                    for (int32_t i = 0; i < batchSize; i++)
                    {
                        lbPt->add_values(lb);
                        ubPt->add_values(ub);
                    }
                }
                break;
                case AttributeType::DISTANCE_PRECISE:
                case AttributeType::DISTANCE_APPROX:
                {
                    const auto [x, y, z, ub] = std::get<PositionQuery<int64_t> >(m_Data);
                    proto::PositionQuery *pos = attr->mutable_position();
                    proto::IntegerPlaintext *xPt = pos->mutable_plain_x();
                    proto::IntegerPlaintext *yPt = pos->mutable_plain_y();
                    proto::IntegerPlaintext *zPt = pos->mutable_plain_z();
                    proto::IntegerPlaintext *ubPt = pos->mutable_plain_upper();

                    for (int32_t i = 0; i < batchSize; i++)
                    {
                        xPt->add_values(x);
                        yPt->add_values(y);
                        zPt->add_values(z);
                        ubPt->add_values(ub);
                    }
                }
                break;
                case AttributeType::TYPE_COUNT:
                    PD_ASSERT(false, "Use of invalid attribute type")
                    break;
            }
        } else
        {
            switch (type)
            {
                case AttributeType::BOOLEAN:
                {
                    const int64_t value = std::get<int64_t>(m_Data);
                    const std::vector vec(batchSize, value);

                    const auto pt = c.m_InformationBoolean.cc->MakePackedPlaintext(vec);
                    const auto ct = c.m_InformationBoolean.cc->Encrypt(pt, c.m_InformationBoolean.taPubKey);
                    attr->set_single_ciphertext(SerializeToString(ct));
                }
                break;
                case AttributeType::ENUM_PRECISE:
                {
                    const int64_t value = std::get<int64_t>(m_Data);
                    const std::vector vec(batchSize, value);

                    const auto pt = c.m_InformationEnumPrecise.cc->MakePackedPlaintext(vec);
                    const auto ct = c.m_InformationEnumPrecise.cc->Encrypt(pt, c.m_InformationEnumPrecise.taPubKey);
                    attr->set_single_ciphertext(SerializeToString(ct));
                }
                break;
                case AttributeType::ENUM_APPROX:
                {
                    const double value = std::get<double>(m_Data);
                    const std::vector vec(batchSize, value);

                    const auto pt = c.m_InformationEnumApprox.cc->MakeCKKSPackedPlaintext(vec);
                    const auto ct = c.m_InformationEnumApprox.cc->Encrypt(pt, c.m_InformationEnumApprox.taPubKey);
                    attr->set_single_ciphertext(SerializeToString(ct));
                }
                break;
                case AttributeType::CONTINUOUS_PRECISE:
                {
                    const auto [lb, ub] = std::get<RangeQuery<int64_t> >(m_Data);
                    const std::vector vecLb(batchSize, lb);
                    const std::vector vecUb(batchSize, ub);

                    const auto ptLb = c.m_InformationContinuousPrecise.cc->MakePackedPlaintext(vecLb);
                    const auto ptUb = c.m_InformationContinuousPrecise.cc->MakePackedPlaintext(vecUb);
                    const auto ctLb = c.m_InformationContinuousPrecise.cc->Encrypt(ptLb, c.m_InformationContinuousPrecise.taPubKey);
                    const auto ctUb = c.m_InformationContinuousPrecise.cc->Encrypt(ptUb, c.m_InformationContinuousPrecise.taPubKey);

                    proto::RangeQuery *protoRange = attr->mutable_range();
                    protoRange->set_ciphertext_lower(SerializeToString(ctLb));
                    protoRange->set_ciphertext_upper(SerializeToString(ctUb));
                }
                break;
                case AttributeType::CONTINUOUS_APPROX:
                {
                    const auto [lb, ub] = std::get<RangeQuery<double> >(m_Data);
                    const std::vector vecLb(batchSize, lb);
                    const std::vector vecUb(batchSize, ub);

                    const auto ptLb = c.m_InformationContinuousApprox.cc->MakeCKKSPackedPlaintext(vecLb);
                    const auto ptUb = c.m_InformationContinuousApprox.cc->MakeCKKSPackedPlaintext(vecUb);
                    const auto ctLb = c.m_InformationContinuousApprox.cc->Encrypt(ptLb, c.m_InformationContinuousApprox.taPubKey);
                    const auto ctUb = c.m_InformationContinuousApprox.cc->Encrypt(ptUb, c.m_InformationContinuousApprox.taPubKey);

                    proto::RangeQuery *protoRange = attr->mutable_range();
                    protoRange->set_ciphertext_lower(SerializeToString(ctLb));
                    protoRange->set_ciphertext_upper(SerializeToString(ctUb));
                }
                break;
                case AttributeType::DISTANCE_PRECISE:
                {
                    const auto [x, y, z, ub] = std::get<PositionQuery<int64_t> >(m_Data);

                    const std::vector vecX(batchSize, x);
                    const std::vector vecY(batchSize, y);
                    const std::vector vecZ(batchSize, z);
                    const std::vector vecUb(batchSize, ub);

                    const auto ptX = c.m_InformationDistancePrecise.cc->MakePackedPlaintext(vecX);
                    const auto ptY = c.m_InformationDistancePrecise.cc->MakePackedPlaintext(vecY);
                    const auto ptZ = c.m_InformationDistancePrecise.cc->MakePackedPlaintext(vecZ);
                    const auto ptUb = c.m_InformationDistancePrecise.cc->MakePackedPlaintext(vecUb);

                    const auto ctX = c.m_InformationDistancePrecise.cc->Encrypt(ptX, c.m_InformationDistancePrecise.taPubKey);
                    const auto ctY = c.m_InformationDistancePrecise.cc->Encrypt(ptY, c.m_InformationDistancePrecise.taPubKey);
                    const auto ctZ = c.m_InformationDistancePrecise.cc->Encrypt(ptZ, c.m_InformationDistancePrecise.taPubKey);
                    const auto ctUb = c.m_InformationDistancePrecise.cc->Encrypt(ptUb, c.m_InformationDistancePrecise.taPubKey);

                    proto::PositionQuery *protoPos = attr->mutable_position();
                    protoPos->set_ciphertext_x(SerializeToString(ctX));
                    protoPos->set_ciphertext_y(SerializeToString(ctY));
                    protoPos->set_ciphertext_z(SerializeToString(ctZ));
                    protoPos->set_ciphertext_upper(SerializeToString(ctUb));
                }
                break;
                case AttributeType::DISTANCE_APPROX:
                {
                    const auto [x, y, z, ub] = std::get<PositionQuery<double> >(m_Data);
                    const std::vector vecX(batchSize, x);
                    const std::vector vecY(batchSize, y);
                    const std::vector vecZ(batchSize, z);
                    const std::vector vecUb(batchSize, ub);

                    const auto ptX = c.m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(vecX);
                    const auto ptY = c.m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(vecY);
                    const auto ptZ = c.m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(vecZ);
                    const auto ptUb = c.m_InformationDistanceApprox.cc->MakeCKKSPackedPlaintext(vecUb);

                    const auto ctX = c.m_InformationDistanceApprox.cc->Encrypt(ptX, c.m_InformationDistanceApprox.taPubKey);
                    const auto ctY = c.m_InformationDistanceApprox.cc->Encrypt(ptY, c.m_InformationDistanceApprox.taPubKey);
                    const auto ctZ = c.m_InformationDistanceApprox.cc->Encrypt(ptZ, c.m_InformationDistanceApprox.taPubKey);
                    const auto ctUb = c.m_InformationDistanceApprox.cc->Encrypt(ptUb, c.m_InformationDistanceApprox.taPubKey);

                    proto::PositionQuery *protoPos = attr->mutable_position();
                    protoPos->set_ciphertext_x(SerializeToString(ctX));
                    protoPos->set_ciphertext_y(SerializeToString(ctY));
                    protoPos->set_ciphertext_z(SerializeToString(ctZ));
                    protoPos->set_ciphertext_upper(SerializeToString(ctUb));
                }
                break;
                case AttributeType::TYPE_COUNT:
                    PD_ASSERT(false, "Use of invalid attribute type")
                    break;
            }
        }
    }

    AttributeType QueryAttributeData::GetAttributeType() const
    {
        return MatchableAttribute::GetAttributeType(m_Attribute);
    }

    std::ostream& operator<<(std::ostream &os, const QueryAttributeData &attr)
    {
        os << "[AttributeData] { " << attr.m_Attribute << ": ";
        if (std::holds_alternative<int64_t>(attr.m_Data))
        {
            os << std::get<int64_t>(attr.m_Data);
        } else if (std::holds_alternative<double>(attr.m_Data))
        {
            os << std::get<double>(attr.m_Data);
        } else if (std::holds_alternative<RangeQuery<int64_t>>(attr.m_Data))
        {
            auto& rq = std::get<RangeQuery<int64_t>>(attr.m_Data);
            os << "(" << rq.lowerBound << "," << rq.upperBound << ")";
        } else if (std::holds_alternative<RangeQuery<double>>(attr.m_Data))
        {
            auto& rq = std::get<RangeQuery<double>>(attr.m_Data);
            os << "(" << rq.lowerBound << "," << rq.upperBound << ")";
        } else if (std::holds_alternative<PositionQuery<int64_t>>(attr.m_Data))
        {
            auto& pq = std::get<PositionQuery<int64_t>>(attr.m_Data);
            os << "(" << pq.x << "," << pq.y << ", " << pq.z << ", " << pq.upperBound << ")";
        } else if (std::holds_alternative<PositionQuery<double>>(attr.m_Data))
        {
            auto& pq = std::get<PositionQuery<double>>(attr.m_Data);
            os << "(" << pq.x << "," << pq.y << ", " << pq.z << ", " << pq.upperBound << ")";
        }
        os << "}";
        return os;
    }

    CombineGroup::CombineGroup(const CombineOperator op): m_Operator(op)
    {
    }

    CombineGroup::CombineGroup(CombineGroup &&other) noexcept : m_Operator(other.m_Operator), m_Payload(std::move(other.m_Payload))
    {
    }

    CombineGroup *CombineGroup::AddCombineGroup(CombineOperator op)
    {
        PD_ASSERT(
            std::holds_alternative<std::monostate>(m_Payload) || std::holds_alternative<std::vector<CombineGroup> >(
                m_Payload), "Variant holds wrong type.")

        if (std::holds_alternative<std::monostate>(m_Payload))
        {
            m_Payload = std::vector<CombineGroup>();
        }

        std::get<std::vector<CombineGroup> >(m_Payload).emplace_back(op);
        return &std::get<std::vector<CombineGroup> >(m_Payload).back();
    }

    QueryAttributeData *CombineGroup::AddAttributeData(const std::string &attr, const bool invertResult)
    {
        PD_ASSERT(
            std::holds_alternative<std::monostate>(m_Payload) || std::holds_alternative<std::vector<QueryAttributeData> >
            (m_Payload), "Variant holds wrong type.")

        if (std::holds_alternative<std::monostate>(m_Payload))
        {
            m_Payload = std::vector<QueryAttributeData>();
        }

        std::get<std::vector<QueryAttributeData> >(m_Payload).emplace_back(attr, invertResult);
        return &std::get<std::vector<QueryAttributeData> >(m_Payload).back();
    }

    void CombineGroup::MoveAttributeData(QueryAttributeData &&attr)
    {
        PD_ASSERT(IsCompatible(attr), "Attribute is not compatible with current combine group.");

        if (std::holds_alternative<std::monostate>(m_Payload))
        {
            m_Payload = std::vector<QueryAttributeData>();
        }

        std::get<std::vector<QueryAttributeData> >(m_Payload).push_back(std::move(attr));
    }

    void CombineGroup::MoveCombineGroup(CombineGroup &&cg)
    {
        PD_ASSERT(IsCompatible(cg), "Combine group is not compatible with current combine group.");

        if (std::holds_alternative<std::monostate>(m_Payload))
        {
            m_Payload = std::vector<CombineGroup>();
        }

        std::get<std::vector<CombineGroup> >(m_Payload).push_back(std::move(cg));
    }

    void CombineGroup::Parse(const nlohmann::json &childrenData, const AttributeType attributeType) // NOLINT(*-no-recursion)
    {
        for (const auto &item: childrenData)
        {
            const std::string &type = item.at("type");

            if (type == "CombineGroup")
            {
                const CombineOperator op = kNameToCombineOperator.at(item.at("operator"));
                CombineGroup *cg = AddCombineGroup(op);

                cg->Parse(item.at("children"), attributeType);
            } else if (type == "Attribute")
            {
                std::string attribute = item.at("attribute");
                bool invertResult = false;
                if (item.contains("invertResult"))
                    invertResult = item.at("invertResult");

                QueryAttributeData *attr = AddAttributeData(attribute, invertResult);
                attr->Parse(item, attributeType);
            }
        }
    }

    void CombineGroup::Pack(proto::CombineGroup *group, const Client &c) const // NOLINT(*-no-recursion)
    {
        PD_ASSERT(!std::holds_alternative<std::monostate>(m_Payload), "CombineGroup was not filled");

        group->set_op(static_cast<proto::CombineOperator>(m_Operator));
        if (std::holds_alternative<std::vector<CombineGroup> >(m_Payload))
        {
            proto::CombineGroupList *list = group->mutable_groups();
            for (const auto &g: std::get<std::vector<CombineGroup> >(m_Payload))
            {
                proto::CombineGroup *innerGroup = list->add_list();
                g.Pack(innerGroup, c);
            }
        } else
        {
            proto::QueryAttributeDataList *list = group->mutable_attribute_data_list();
            for (const auto &a: std::get<std::vector<QueryAttributeData> >(m_Payload))
            {
                proto::QueryAttributeData *attr = list->add_list();
                a.Pack(attr, c);
            }
        }
    }

    AttributeType CombineGroup::GetAttributeType() const
    {
        PD_ASSERT(!std::holds_alternative<std::monostate>(m_Payload), "CombineGroup was not filled");

        if (std::holds_alternative<std::vector<QueryAttributeData>>(m_Payload))
            return std::get<std::vector<QueryAttributeData>>(m_Payload).at(0).GetAttributeType();

        return std::get<std::vector<CombineGroup>>(m_Payload).at(0).GetAttributeType();
    }

    bool CombineGroup::IsCompatible(const QueryAttributeData &attr) const
    {
        if (std::holds_alternative<std::monostate>(m_Payload))
            return true;

        if (std::holds_alternative<std::vector<CombineGroup>>(m_Payload))
            return false;

        const auto& vec = std::get<std::vector<QueryAttributeData>>(m_Payload);
        if (vec.empty())
            return true;

        return vec.front().GetAttributeType() == attr.GetAttributeType();
    }

    bool CombineGroup::IsCompatible(const CombineGroup &cg) const
    {
        if (std::holds_alternative<std::monostate>(m_Payload))
            return true;

        if (std::holds_alternative<std::vector<QueryAttributeData>>(m_Payload))
            return false;

        const auto& vec = std::get<std::vector<CombineGroup>>(m_Payload);
        if (vec.empty())
            return true;

        return vec.front().GetAttributeType() == cg.GetAttributeType();
    }

    std::ostream& operator<<(std::ostream &os, const CombineGroup &attr)
    {
        os << "[CombineGroup] (" << attr.m_Operator << ") {";
        if (std::holds_alternative<std::vector<CombineGroup>>(attr.m_Payload))
        {
            for (const auto &g: std::get<std::vector<CombineGroup>>(attr.m_Payload))
            {
                os << g << ", ";
            }
        } else if (std::holds_alternative<std::vector<QueryAttributeData>>(attr.m_Payload))
        {
            for (const auto &a: std::get<std::vector<QueryAttributeData>>(attr.m_Payload))
            {
                os << a << ", ";
            }
        }
        os << "}";

        return os;
    }

    QueryBuilder::QueryBuilder() = default;

    QueryBuilder::QueryBuilder(QueryBuilder && other) noexcept
        : m_CombineGroups(std::move(other.m_CombineGroups))
    {
    }

    CombineGroup *QueryBuilder::AddCombineGroup(AttributeType type, CombineOperator op)
    {
        PD_ASSERT(!m_CombineGroups.contains(type), "Type is already present in map");
        m_CombineGroups.emplace(type, op);

        return &m_CombineGroups.at(type);
    }

    void QueryBuilder::MoveCombineGroup(AttributeType type, CombineGroup &&cg)
    {
        PD_ASSERT(!m_CombineGroups.contains(type), "Type is already present in map");
        m_CombineGroups.insert({type, std::move(cg)});
    }

    void QueryBuilder::Parse(const std::string &fileName)
    {
        std::ifstream fs(fileName);

        if (fs.is_open())
        {
            nlohmann::json jsonData = nlohmann::json::parse(fs);

            for (const auto &item: jsonData)
            {
                const AttributeType type = kNameToAttributeType.at(item.at("attributeType"));
                const CombineOperator op = kNameToCombineOperator.at(item.at("operator"));
                CombineGroup *cg = AddCombineGroup(type, op);

                cg->Parse(item.at("children"), type);
            }
        } else
        {
            PD_ASSERT(false, "Failed to open query file")
        }
    }

    void QueryBuilder::Pack(const Client &c, proto::QueryData *msg)
    {
        for (const auto &[type, group]: m_CombineGroups)
        {
            proto::QueryMapEntry *entry = msg->add_entries();
            entry->set_attribute_type(static_cast<proto::AttributeType>(type));

            proto::CombineGroup *protoGroup = entry->mutable_group();
            group.Pack(protoGroup, c);
        }
    }

    void QueryBuilder::GetUsedAttributeTypes(std::set<AttributeType> &outUsed)
    {
        for (const auto &type: m_CombineGroups | std::views::keys)
        {
            outUsed.insert(type);
        }
    }

    void QueryBuilder::Print() const
    {
        for (auto& [key, value] : m_CombineGroups)
        {
            std::cout << "[" << key << "]" << value << std::endl;
        }
    }
}

