#ifndef COMMUNICATIONCOST_HPP
#define COMMUNICATIONCOST_HPP

namespace pat_disc {
    class CommunicationCost
    {
    public:
        static CommunicationCost &GetInstance();

        void SwitchLabel(const std::string &label);

        void RecordMessageSize(uint64_t messageSize);

        const std::map<std::string, uint64_t>& GetResults();

    private:
        static CommunicationCost *s_Instance;

        std::string m_CurrentLabel;
        std::map<std::string, uint64_t> m_CommunicationCost;
    };
}

#endif //COMMUNICATIONCOST_HPP
