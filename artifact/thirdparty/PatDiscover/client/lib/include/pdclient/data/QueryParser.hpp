//
// Created by Niels Pressel on 17.10.2025.
//

#ifndef PAT_DISC_QUERYPARSER_HPP
#define PAT_DISC_QUERYPARSER_HPP

#include "QueryBuilder.hpp"

namespace pat_disc {
    class QueryParser
    {
    public:
        QueryParser() = default;

        static QueryBuilder ParseQueryString(const std::string& q);
    };
}

#endif //PAT_DISC_QUERYPARSER_HPP