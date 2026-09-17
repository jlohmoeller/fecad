#include <gtest/gtest.h>
#include <pdserver/math/Polynomial.hpp>


TEST(CompositePolynomial, Parse)
{
    const auto result = pat_disc::CompositePolynomial<double>::Parse("data/polynomials/distance_approx.poly");

    std::cout << "Done" << std::endl;
}

