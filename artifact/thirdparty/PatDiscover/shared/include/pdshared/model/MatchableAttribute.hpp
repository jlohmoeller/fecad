#ifndef SERVER_MATCHABLEATTRIBUTE_HPP
#define SERVER_MATCHABLEATTRIBUTE_HPP

namespace pat_disc {

    template<typename T>
    struct Position
    {
        T x, y, z;
    };

    enum class AttributeType : uint_fast8_t
    {
        BOOLEAN = 0,
        ENUM_PRECISE,
        ENUM_APPROX,
        CONTINUOUS_PRECISE,
        CONTINUOUS_APPROX,
        DISTANCE_PRECISE,
        DISTANCE_APPROX,

        // -- No real type --
        TYPE_COUNT
    };

    std::ostream& operator<<(std::ostream& os, const AttributeType &attr);

    extern const std::map<AttributeType, std::string> kAttributeTypeNames;
    extern const std::map<std::string, AttributeType> kNameToAttributeType;

    class AttributeTypes
    {
    public:
        class AttributeIterator
        {
        public:
            explicit AttributeIterator(AttributeType start) : m_Current(start)
            {
            }

            AttributeIterator &operator++()
            {
                m_Current = static_cast<AttributeType>(static_cast<uint_fast8_t>(m_Current) + 1);
                return *this;
            }

            AttributeIterator operator++(int)
            {
                AttributeIterator ret = *this;
                ++(*this);
                return ret;
            }

            bool operator==(AttributeIterator other) const
            {
                return m_Current == other.m_Current;
            }

            bool operator!=(AttributeIterator other) const
            {
                return !(*this == other);
            }

            AttributeType operator*()
            {
                return m_Current;
            }

        private:
            AttributeType m_Current = AttributeType::BOOLEAN;
        };

        static AttributeIterator begin()
        {
            return AttributeIterator(AttributeType::BOOLEAN);
        }

        static AttributeIterator end()
        {
            return AttributeIterator(AttributeType::TYPE_COUNT);
        }
    };


    class MatchableAttribute
    {
    public:
        explicit MatchableAttribute(const std::string& attribute);

        virtual ~MatchableAttribute() = default;

        static uint32_t GetAttributeCountForType(AttributeType type);

        static uint32_t GetEffectiveAttributeCountForType(AttributeType type);

        static uint32_t GetBatchSizeForType(AttributeType type);

        static AttributeType GetAttributeType(const std::string& attribute);

        static void Parse(const std::string &filePath);

        static const std::map<std::string, std::shared_ptr<MatchableAttribute> >& GetAttributes();

        virtual AttributeType GetAttributeType() = 0;

    protected:
        static std::map<std::string, std::shared_ptr<MatchableAttribute> > s_PatientAttributes;

        std::string m_Attribute;
    };

    class BooleanAttribute final: public MatchableAttribute
    {
    public:
        explicit BooleanAttribute(const std::string& attr);

        static BooleanAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;
    };

    class PreciseEnumAttribute final : public MatchableAttribute
    {
    public:
        explicit PreciseEnumAttribute(const std::string& attr);

        static PreciseEnumAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;
    };

    class ApproxEnumAttribute final : public MatchableAttribute
    {
    public:
        ApproxEnumAttribute(const std::string& attr, int32_t minValue, int32_t maxValue);

        static ApproxEnumAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;

        [[nodiscard]] double ConvertValue(int64_t value) const;

    private:
        int32_t m_MinValue;
        int32_t m_MaxValue;
    };

    class PreciseContinuousAttribute final : public MatchableAttribute
    {
    public:
        PreciseContinuousAttribute(const std::string& attr, int32_t decimalPrecision);

        static PreciseContinuousAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;

        [[nodiscard]] int64_t ConvertValue(double value) const;

    private:
        int32_t m_DecimalPrecision;
    };

    class ApproxContinuousAttribute final : public MatchableAttribute
    {
    public:
        ApproxContinuousAttribute(const std::string& attr, double minValue, double maxValue, int32_t decimalPrecision);

        static ApproxContinuousAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;

        [[nodiscard]] double ConvertValue(double value) const;

        [[nodiscard]] int64_t ConvertPlaintextValue(double value) const;

    private:
        double m_MinValue;
        double m_MaxValue;

        // Required for plaintext calculations
        int32_t m_DecimalPrecision;
    };

    class PreciseDistanceAttribute final : public MatchableAttribute
    {
    public:
        PreciseDistanceAttribute(const std::string& attr, int32_t decimalPrecision);

        static PreciseDistanceAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;

        [[nodiscard]] Position<int64_t> ConvertPosition(const Position<double> &pos) const;

        [[nodiscard]] int64_t ConvertBoundValue(double value) const;

    private:
        [[nodiscard]] int64_t ConvertDataValue(double value) const;

    private:
        int32_t m_DecimalPrecision;
    };

    class ApproxDistanceAttribute final : public MatchableAttribute
    {
    public:
        ApproxDistanceAttribute(const std::string& attr, double xMin, double xMax, double yMin, double yMax, double zMin, double zMax, int32_t decimalPrecision);

        static ApproxDistanceAttribute &Get(const std::string& attr);

        AttributeType GetAttributeType() override;

        [[nodiscard]] Position<double> ConvertValue(const Position<double> &pos) const;

        [[nodiscard]] Position<int64_t> ConvertPlaintextValue(const Position<double> &pos) const;

        [[nodiscard]] double ConvertUpperBoundValue(double upperBound) const;

        [[nodiscard]] int64_t ConvertPlaintextUpperBoundValue(double upperBound) const;

    private:
        double m_MinX, m_MinY, m_MinZ;
        double m_MaxX, m_MaxY, m_MaxZ;

        int32_t m_DecimalPrecision;
    };
}

#endif //SERVER_MATCHABLEATTRIBUTE_HPP
