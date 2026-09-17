#ifndef QUERYPROCESSINGPRIMITIVE_HPP
#define QUERYPROCESSINGPRIMITIVE_HPP


namespace pat_disc {
    class QueryProcessingPrimitive
    {
    public:
        static IntegerEncoding EnumMatching(const IntegerEncoding &data, const IntegerEncoding &query);

        static IntegerEncoding ContinuousMatching(const IntegerEncoding &data, const IntegerEncoding &queryLower, const IntegerEncoding &queryUpper);

        static IntegerEncoding DistanceMatching(const IntegerEncoding &dataX, const IntegerEncoding &dataY, const IntegerEncoding &dataZ, const IntegerEncoding &queryX,
                                                const IntegerEncoding &queryY, const IntegerEncoding &queryZ, const IntegerEncoding &upperBound);

        static IntegerEncoding Sum(const std::vector<IntegerEncoding>& data);

        static IntegerEncoding And(const std::vector<IntegerEncoding>& data);

        static IntegerEncoding Or(const std::vector<IntegerEncoding>& data);

        static IntegerEncoding Not(const IntegerEncoding& data);
    };
}


#endif //QUERYPROCESSINGPRIMITIVE_HPP
