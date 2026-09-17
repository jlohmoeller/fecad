#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <spdlog/spdlog.h>

namespace pat_disc {
    class Logger
    {
    public:
        static void Initialize(const std::string &entity, const std::string &runId);

        static void Shutdown();

        static std::shared_ptr<spdlog::logger> GetLogger();

    private:
        static std::shared_ptr<spdlog::logger> s_Logger;
    };
}

#define PD_TRACE(...) ::pat_disc::Logger::GetLogger()->trace(__VA_ARGS__)
#define PD_INFO(...) ::pat_disc::Logger::GetLogger()->info(__VA_ARGS__)
#define PD_WARN(...) ::pat_disc::Logger::GetLogger()->warn(__VA_ARGS__)
#define PD_ERROR(...) ::pat_disc::Logger::GetLogger()->error(__VA_ARGS__)

#endif //LOGGER_HPP
