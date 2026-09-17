#include "pdshared/measure/Timer.hpp"

#include "pdshared/util/Utilities.hpp"

namespace pat_disc {
    Timer *Timer::s_Instance = new Timer();


    Timer &Timer::GetInstance()
    {
        return *s_Instance;
    }

    void Timer::Initialize(const std::string &label)
    {
        m_Start = std::chrono::high_resolution_clock::now();
        m_CurrentLabel = label;
    }

    void Timer::Switch(const std::string &label)
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_Start).count();

        if (m_Timings.contains(m_CurrentLabel))
        {
            m_Timings.at(m_CurrentLabel) += duration;
        } else
        {
            m_Timings.insert({m_CurrentLabel, duration});
        }

        m_Start = end;
        m_CurrentLabel = label;
    }

    const std::map<std::string, int64_t> &Timer::GetResults()
    {
        return m_Timings;
    }

    void Timer::Shutdown()
    {
        const auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_Start).count();

        if (m_Timings.contains(m_CurrentLabel))
        {
            m_Timings.at(m_CurrentLabel) += duration;
        } else
        {
            m_Timings.insert({m_CurrentLabel, duration});
        }
    }
}

