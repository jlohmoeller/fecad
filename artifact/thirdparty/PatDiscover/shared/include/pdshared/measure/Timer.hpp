#ifndef SERVER_TIMER_HPP
#define SERVER_TIMER_HPP

namespace pat_disc {
    class Timer
    {
    public:
        static Timer &GetInstance();

        void Initialize(const std::string &label);

        void Switch(const std::string &label);

        void Shutdown();

        const std::map<std::string, int64_t> &GetResults();

    private:
        static Timer *s_Instance;

        std::map<std::string, int64_t> m_Timings;
        std::string m_CurrentLabel;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    };
}

#endif //SERVER_TIMER_HPP
