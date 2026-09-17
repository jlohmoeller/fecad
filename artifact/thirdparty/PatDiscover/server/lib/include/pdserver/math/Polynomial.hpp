#ifndef POLYNOMIAL_HPP
#define POLYNOMIAL_HPP

namespace pat_disc {
    template<typename T>
    class Polynomial
    {
    public:
        static std::shared_ptr<Polynomial> Parse(const std::string &line)
        {
            std::vector<T> coeffs;
            std::stringstream ss(line);

            std::string token;
            while (std::getline(ss, token, ' '))
            {
                coeffs.push_back(static_cast<T>(std::stod(token)));
            }

            return std::make_shared<Polynomial>(std::move(coeffs));
        }

        explicit Polynomial(const std::vector<T> &coeffs)
            : m_Coefficients(coeffs)
        {
        }

        explicit Polynomial(std::vector<T> &&coeffs)
            : m_Coefficients(std::move(coeffs))
        {
        }

        [[nodiscard]] uint32_t Degree() const
        {
            uint32_t result = m_Coefficients.size() - 1;
            while (result > 0)
            {
                if (m_Coefficients[result] != 0.0)
                    break;

                result--;
            }

            return result;
        }

        [[nodiscard]] size_t Size() const
        {
            return m_Coefficients.size();
        }

        [[nodiscard]] bool isNull() const
        {
            return std::ranges::all_of(m_Coefficients, [](auto c) { return c == 0.0; });
        }

        [[nodiscard]] T GetAt(size_t index) const
        {
            return m_Coefficients[index];
        }

        void SetAt(size_t index, T value)
        {
            if (index >= m_Coefficients.size())
            {
                m_Coefficients.resize(index + 1);
            }

            m_Coefficients[index] = value;
        }

        void DivideChebyshev(const Polynomial &divisor, Polynomial &outQuotient, Polynomial &outRemainder) const
        {
            const auto result = lbcrypto::LongDivisionChebyshev(m_Coefficients, divisor.m_Coefficients);

            outQuotient.m_Coefficients = std::move(result->q);
            outRemainder.m_Coefficients = std::move(result->r);

            outQuotient.RemoveLeadingZeros();
            outRemainder.RemoveLeadingZeros();
        }

        void RemoveLeadingZeros()
        {
            while (!m_Coefficients.empty() && m_Coefficients.back() == 0.0)
            {
                m_Coefficients.pop_back();
            }
        }

    private:
        std::vector<T> m_Coefficients;
    };

    template<typename T>
    class CompositePolynomial
    {
    public:
        static std::shared_ptr<CompositePolynomial> Parse(const std::string &filepath)
        {
            std::vector<std::shared_ptr<Polynomial<T> > > result;

            std::ifstream file(filepath);
            std::string line;
            while (std::getline(file, line))
            {
                result.push_back(Polynomial<T>::Parse(line));
            }

            return std::make_shared<CompositePolynomial>(std::move(result));
        }

        explicit CompositePolynomial(const std::vector<std::shared_ptr<Polynomial<T> > > &polynomials)
            : m_Polynomials(polynomials)
        {
        }

        explicit CompositePolynomial(std::vector<std::shared_ptr<Polynomial<T> > > &&polynomials)
            : m_Polynomials(std::move(polynomials))
        {
        }

        typename std::vector<std::shared_ptr<Polynomial<T> > >::iterator begin()
        {
            return m_Polynomials.begin();
        }

        typename std::vector<std::shared_ptr<Polynomial<T> > >::iterator end()
        {
            return m_Polynomials.end();
        }

    private:
        std::vector<std::shared_ptr<Polynomial<T> > > m_Polynomials;
    };
}

#endif //POLYNOMIAL_HPP
