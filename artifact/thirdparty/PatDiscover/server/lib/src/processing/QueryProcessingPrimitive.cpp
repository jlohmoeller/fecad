#include "pdserver/processing/QueryProcessingPrimitive.hpp"

namespace pat_disc {
    IntegerEncoding QueryProcessingPrimitive::EnumMatching(const IntegerEncoding &data, const IntegerEncoding &query)
    {
        PD_ASSERT(data->size() == query->size(), "Data and query sizes don't match");
        const size_t dataCount = data->size();

        auto result = std::make_shared<std::vector<int64_t> >(dataCount);
        for (size_t i = 0; i < dataCount; i++)
        {
            result->at(i) = data->at(i) == query->at(i) ? 1 : 0;
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::ContinuousMatching(const IntegerEncoding &data, const IntegerEncoding &queryLower, const IntegerEncoding &queryUpper)
    {
        PD_ASSERT(data->size() == queryLower->size() && data->size() == queryUpper->size(), "Data and query sizes don't match");
        const size_t dataCount = data->size();

        auto result = std::make_shared<std::vector<int64_t> >(dataCount);
        for (size_t i = 0; i < dataCount; i++)
        {
            result->at(i) = data->at(i) > queryLower->at(i) && data->at(i) < queryUpper->at(i) ? 1 : 0;
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::DistanceMatching(const IntegerEncoding &dataX, const IntegerEncoding &dataY, const IntegerEncoding &dataZ, const IntegerEncoding &queryX,
                                                            const IntegerEncoding &queryY, const IntegerEncoding &queryZ, const IntegerEncoding &upperBound)
    {
        PD_ASSERT(dataX->size() == dataY->size(), "Data and query sizes don't match");
        PD_ASSERT(dataY->size() == dataZ->size(), "Data and query sizes don't match");
        PD_ASSERT(dataZ->size() == queryX->size(), "Data and query sizes don't match");
        PD_ASSERT(queryX->size() == queryY->size(), "Data and query sizes don't match");
        PD_ASSERT(queryY->size() == queryZ->size(), "Data and query sizes don't match");
        PD_ASSERT(queryZ->size() == upperBound->size(), "Data and query sizes don't match");

        const size_t dataCount = dataX->size();

        auto result = std::make_shared<std::vector<int64_t> >(dataCount);
        for (size_t i = 0; i < dataCount; i++)
        {
            int64_t xDiff = dataX->at(i) - queryX->at(i);
            int64_t yDiff = dataY->at(i) - queryY->at(i);
            int64_t zDiff = dataZ->at(i) - queryZ->at(i);
            xDiff = xDiff * xDiff;
            yDiff = yDiff * yDiff;
            zDiff = zDiff * zDiff;

            const int64_t sum = xDiff + yDiff + zDiff;

            result->at(i) = sum < upperBound->at(i) ? 1 : 0;
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::Sum(const std::vector<IntegerEncoding> &data)
    {
        PD_ASSERT(!data.empty(), "Called sum operator on empty vector");

        if (data.size() == 1)
        {
            return data[0];
        }

        const auto result = std::make_shared<std::vector<int64_t>>(data[0]->size());

        for (const auto& item : data)
        {
            for (int i = 0; i < item->size(); i++)
            {
                result->at(i) += item->at(i);
            }
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::And(const std::vector<IntegerEncoding> &data)
    {
        PD_ASSERT(!data.empty(), "Called and operator on empty vector");

        if (data.size() == 1)
        {
            return data[0];
        }

        const auto result = std::make_shared<std::vector<int64_t>>(data[0]->size(), 1);

        for (const auto& item : data)
        {
            for (int i = 0; i < item->size(); i++)
            {
                result->at(i) = static_cast<int64_t>(static_cast<bool>(item->at(i)) && static_cast<bool>(result->at(i)));
            }
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::Or(const std::vector<IntegerEncoding> &data)
    {
        PD_ASSERT(!data.empty(), "Called or operator on empty vector");

        if (data.size() == 1)
        {
            return data[0];
        }

        const auto result = std::make_shared<std::vector<int64_t>>(data[0]->size(), 0);

        for (const auto& item : data)
        {
            for (int i = 0; i < item->size(); i++)
            {
                result->at(i) = static_cast<int64_t>(static_cast<bool>(item->at(i)) || static_cast<bool>(result->at(i)));
            }
        }

        return result;
    }

    IntegerEncoding QueryProcessingPrimitive::Not(const IntegerEncoding& data)
    {
        const auto result = std::make_shared<std::vector<int64_t>>(data->size(), 0);
        for (int i = 0; i < data->size(); i++)
        {
            result->at(i) = 1 - data->at(i);
        }

        return result;
    }
}
