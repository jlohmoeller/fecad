#ifndef SERVER_QUERYBUILDER_HPP
#define SERVER_QUERYBUILDER_HPP

#include <pdproto/data_transfer_objects.pb.h>

#include "pdclient/entity/Client.hpp"

namespace pat_disc {
    class QueryBuilder;

    enum class CombineOperator { AND = 0, OR, SUM };

    template<typename T>
    struct RangeQuery
    {
        T lowerBound;
        T upperBound;
    };

    template<typename T>
    struct PositionQuery
    {
        T x, y, z;
        T upperBound;
    };

    class QueryAttributeData
    {
    public:
        explicit QueryAttributeData(const std::string& attr, bool invertResult);
        QueryAttributeData(const QueryAttributeData &other) = default;
        QueryAttributeData(QueryAttributeData&& other) = default;

        void SetBooleanValue(int64_t value);

        void SetPreciseEnumValue(int64_t value);

        void SetApproxEnumValue(int64_t value);

        void SetPreciseContinuousValue(double lb, double ub);

        void SetApproxContinuousValue(double lb, double ub);

        void SetPreciseDistanceValue(double x, double y, double z, double ub);

        void SetApproxDistanceValue(double x, double y, double z, double ub);

        void Parse(const nlohmann::json &attrData, AttributeType attributeType);

        void Pack(proto::QueryAttributeData *attr, const Client &c) const;

        AttributeType GetAttributeType() const;

        friend std::ostream& operator<<(std::ostream &os, const QueryAttributeData &attr);

    private:
        std::string m_Attribute;
        bool m_InvertResult;
        std::variant<std::monostate, int64_t, double, RangeQuery<int64_t>, RangeQuery<double>, PositionQuery<int64_t>, PositionQuery<double>> m_Data;
    };

    class CombineGroup
    {
    public:
        explicit CombineGroup(CombineOperator op);
        CombineGroup(const CombineGroup &other) = default;
        CombineGroup(CombineGroup&& other) noexcept;

        CombineGroup *AddCombineGroup(CombineOperator op);

        QueryAttributeData *AddAttributeData(const std::string& attr, bool invertResult);

        void MoveAttributeData(QueryAttributeData&& attr);

        void MoveCombineGroup(CombineGroup&& cg);

        void Parse(const nlohmann::json &childrenData, AttributeType attributeType);

        void Pack(proto::CombineGroup *group, const Client &c) const;

        AttributeType GetAttributeType() const;

        friend std::ostream& operator<<(std::ostream &os, const CombineGroup &attr);

    private:
        bool IsCompatible(const QueryAttributeData& attr) const;
        bool IsCompatible(const CombineGroup& cg) const;

    private:
        CombineOperator m_Operator;
        std::variant<std::monostate, std::vector<CombineGroup>, std::vector<QueryAttributeData> > m_Payload;
    };

    class QueryBuilder
    {
    public:
        QueryBuilder();
        QueryBuilder(const QueryBuilder &other) = delete;
        QueryBuilder(QueryBuilder&&) noexcept;

        CombineGroup *AddCombineGroup(AttributeType type, CombineOperator op);

        void MoveCombineGroup(AttributeType type, CombineGroup&& cg);

        void Parse(const std::string &fileName);

        void Pack(const Client &c, proto::QueryData *msg);

        void GetUsedAttributeTypes(std::set<AttributeType>& outUsed);

        void Print() const;

    private:
        std::map<AttributeType, CombineGroup> m_CombineGroups;
    };
}

#endif //SERVER_QUERYBUILDER_HPP
