// shim: no-op Timer
// Switch() is only reached from the CKKS scheme-switch path the approximate-only
// binding never enters, but the symbol must still resolve at link time
#ifndef SERVER_TIMER_HPP
#define SERVER_TIMER_HPP

#include <chrono>
#include <map>
#include <string>

namespace pat_disc {
    class Timer
    {
    public:
        static Timer &GetInstance() { static Timer t; return t; }
        void Initialize(const std::string &) {}
        void Switch(const std::string &) {}
        void Shutdown() {}
        const std::map<std::string, int64_t> &GetResults() { return m_Timings; }

    private:
        std::map<std::string, int64_t> m_Timings;
    };
}

#endif //SERVER_TIMER_HPP
