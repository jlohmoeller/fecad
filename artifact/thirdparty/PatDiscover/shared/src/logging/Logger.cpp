#include "pdshared/logging/Logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace pat_disc {
    std::shared_ptr<spdlog::logger> Logger::s_Logger;

    void Logger::Initialize(const std::string &entity, const std::string &runId)
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::format("logs/{}/{}.log", entity, runId)));
        s_Logger = std::make_shared<spdlog::logger>("core", std::begin(sinks), std::end(sinks));
        s_Logger->set_level(spdlog::level::trace);
        s_Logger->flush_on(spdlog::level::trace);
    }

    void Logger::Shutdown()
    {
        spdlog::shutdown();
    }

    std::shared_ptr<spdlog::logger> Logger::GetLogger()
    {
        return s_Logger;
    }
}
