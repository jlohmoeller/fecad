#include "pdshared/measure/CommunicationCost.hpp"

namespace pat_disc {
    CommunicationCost *CommunicationCost::s_Instance = new CommunicationCost;

    CommunicationCost &CommunicationCost::GetInstance()
    {
        return *s_Instance;
    }

    void CommunicationCost::SwitchLabel(const std::string &label)
    {
        m_CurrentLabel = label;
    }

    void CommunicationCost::RecordMessageSize(const uint64_t messageSize)
    {
        if (!m_CommunicationCost.contains(m_CurrentLabel))
        {
            m_CommunicationCost.insert({m_CurrentLabel, 0});
        }

        m_CommunicationCost.at(m_CurrentLabel) += messageSize;
    }

    const std::map<std::string, uint64_t> &CommunicationCost::GetResults()
    {
        return m_CommunicationCost;
    }
}
