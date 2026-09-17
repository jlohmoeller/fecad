#ifndef SERVER_QUERYDATA_HPP
#define SERVER_QUERYDATA_HPP

namespace pat_disc {
    class QueryBooleanAttribute
    {
    public:
        enum Value : uint_fast8_t
        {
            YES = 0,
            NO = 1,
            UNKNOWN = 2,
            DONT_CARE
        };

        QueryBooleanAttribute() = default;

        constexpr explicit QueryBooleanAttribute(const Value val) : value(val)
        {
        }

        [[nodiscard]] int64_t GetIntValue() const
        {
            return value;
        }

        [[nodiscard]] double GetDoubleValue() const
        {
            return value;
        }

    private:
        Value value;
    };

    class QueryTumorType
    {
    public:
        enum Value : uint_fast8_t
        {
            GLIOBLASTOMA = 0,
            OLIGODENDROGLIOMA,
            ASTROCYTOMA,
            BRAIN_METASTASIS,
            UNKNOWN,
            DONT_CARE
        };

        QueryTumorType() = default;

        constexpr explicit QueryTumorType(const Value val) : value(val)
        {
        }

        [[nodiscard]] int64_t GetIntValue() const
        {
            return value;
        }

        [[nodiscard]] double GetDoubleValue() const
        {
            return value;
        }

    private:
        Value value;
    };

    class QueryWHOGrade
    {
    public:
        enum Value : uint_fast8_t
        {
            I = 0,
            II,
            III,
            IV,
            UNKNOWN,
            DONT_CARE
        };

        QueryWHOGrade() = default;

        constexpr explicit QueryWHOGrade(const Value val) : value(val)
        {
        }

        [[nodiscard]] int64_t GetIntValue() const
        {
            return value;
        }

        [[nodiscard]] double GetDoubleValue() const
        {
            return value;
        }

    private:
        Value value;
    };
}

#endif //SERVER_QUERYDATA_HPP
