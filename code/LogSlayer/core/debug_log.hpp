#pragma once

#include <log4cplus/configurator.h>
#include <log4cplus/initializer.h>
#include <log4cplus/logger.h>
#include <log4cplus/loglevel.h>
#include <log4cplus/tstring.h>

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

#if defined(__linux__)
#    include <unistd.h>
#endif

namespace slayerlog
{
namespace debug_log
{

enum class Severity
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

inline std::filesystem::path executable_directory(const char* argv0)
{
#ifdef _WIN32
    std::array<char, MAX_PATH> module_path {};
    const auto module_path_length = GetModuleFileNameA(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (module_path_length > 0 && module_path_length < module_path.size())
    {
        return std::filesystem::path(std::string(module_path.data(), module_path_length)).parent_path();
    }
#endif

#if defined(__linux__)
    std::array<char, 4096> module_path {};
    const auto module_path_length = readlink("/proc/self/exe", module_path.data(), module_path.size());
    if (module_path_length > 0)
    {
        return std::filesystem::path(std::string(module_path.data(), static_cast<std::size_t>(module_path_length))).parent_path();
    }
#endif

    try
    {
        if (argv0 != nullptr)
        {
            const std::filesystem::path executable_path(argv0);
            if (executable_path.has_parent_path())
            {
                std::error_code error;
                const auto absolute_path = std::filesystem::absolute(executable_path, error);
                if (!error)
                {
                    return absolute_path.parent_path();
                }

                return executable_path.parent_path();
            }
        }

        return std::filesystem::current_path();
    }
    catch (...)
    {
        return ".";
    }
}

struct RuntimePaths
{
    std::filesystem::path executable_dir;
    std::filesystem::path config_file;
    std::filesystem::path log_file;
};

inline RuntimePaths& runtime_paths()
{
    static RuntimePaths paths;
    return paths;
}

inline std::unique_ptr<log4cplus::Initializer>& log4cplus_initializer()
{
    static std::unique_ptr<log4cplus::Initializer> initializer;
    return initializer;
}

inline log4cplus::Logger& logger()
{
    static log4cplus::Logger instance = log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("slayerlog"));
    return instance;
}

inline log4cplus::LogLevel to_log_level(Severity severity)
{
    switch (severity)
    {
    case Severity::Trace:
        return log4cplus::TRACE_LOG_LEVEL;
    case Severity::Debug:
        return log4cplus::DEBUG_LOG_LEVEL;
    case Severity::Info:
        return log4cplus::INFO_LOG_LEVEL;
    case Severity::Warning:
        return log4cplus::WARN_LOG_LEVEL;
    case Severity::Error:
        return log4cplus::ERROR_LOG_LEVEL;
    default:
        return log4cplus::INFO_LOG_LEVEL;
    }
}

inline void write_bootstrap_log_line(const std::string& message)
{
    try
    {
        std::ofstream output(runtime_paths().log_file, std::ios::binary | std::ios::app);
        output << message << '\n';
    }
    catch (...)
    {
    }
}

inline void configure_logging()
{
    try
    {
        auto properties = log4cplus::helpers::Properties(LOG4CPLUS_C_STR_TO_TSTRING(runtime_paths().config_file.string().c_str()), log4cplus::helpers::Properties::fThrow);
        properties.setProperty(LOG4CPLUS_TEXT("log4cplus.appender.ROLLING.File"), LOG4CPLUS_C_STR_TO_TSTRING(runtime_paths().log_file.string().c_str()));
        log4cplus::PropertyConfigurator(properties).configure();
    }
    catch (const std::exception& ex)
    {
        write_bootstrap_log_line("[bootstrap] Failed to configure log4cplus from " + runtime_paths().config_file.string() + ": " + ex.what());
        write_bootstrap_log_line("[bootstrap] Falling back to basic console logging; requested log file is " + runtime_paths().log_file.string());
        log4cplus::BasicConfigurator config;
        config.configure();
    }
    catch (...)
    {
        write_bootstrap_log_line("[bootstrap] Failed to configure log4cplus from " + runtime_paths().config_file.string() + ": unknown error");
        write_bootstrap_log_line("[bootstrap] Falling back to basic console logging; requested log file is " + runtime_paths().log_file.string());
        log4cplus::BasicConfigurator config;
        config.configure();
    }
}

inline void initialize(const char* argv0 = nullptr)
{
    static std::once_flag initialized;
    std::call_once(initialized,
                   [argv0]
                   {
                       runtime_paths().executable_dir = executable_directory(argv0);
                       runtime_paths().config_file    = runtime_paths().executable_dir / "log4cplus.ini";
                       runtime_paths().log_file       = runtime_paths().executable_dir / "slayerlog_debug.log";

                       std::error_code error;
                       std::filesystem::create_directories(runtime_paths().executable_dir, error);

                       log4cplus_initializer() = std::make_unique<log4cplus::Initializer>();
                       configure_logging();
                   });
}

inline const std::filesystem::path& log_file_path()
{
    initialize();
    return runtime_paths().log_file;
}

/// Cheap pre-check so callers can skip building the message entirely when the
/// severity is filtered out (hot paths log whole chunks/lines at trace level).
inline bool level_enabled(Severity severity)
{
    initialize();
    return logger().isEnabledFor(to_log_level(severity));
}

inline void write(Severity severity, const char* file, int line, const char* function_name, const std::string& message)
{
    initialize();
    logger().log(to_log_level(severity), LOG4CPLUS_C_STR_TO_TSTRING(message.c_str()), file, line, function_name);
}

} // namespace debug_log
} // namespace slayerlog

// The level check runs before the message expression so disabled levels never
// pay for message construction (hot paths stream whole chunks into these).
#define SLAYERLOG_LOG_WITH_SEVERITY(severity, message)                                                             \
    do                                                                                                             \
    {                                                                                                              \
        if (::slayerlog::debug_log::level_enabled(severity))                                                       \
        {                                                                                                          \
            std::ostringstream slayerlog_debug_stream__;                                                           \
            slayerlog_debug_stream__ << message;                                                                   \
            ::slayerlog::debug_log::write(severity, __FILE__, __LINE__, __func__, slayerlog_debug_stream__.str()); \
        }                                                                                                          \
    } while (0)

#define SLAYERLOG_LOG_ERROR(message) SLAYERLOG_LOG_WITH_SEVERITY(::slayerlog::debug_log::Severity::Error, message)

#define SLAYERLOG_LOG_WARNING(message) SLAYERLOG_LOG_WITH_SEVERITY(::slayerlog::debug_log::Severity::Warning, message)

#define SLAYERLOG_LOG_INFO(message) SLAYERLOG_LOG_WITH_SEVERITY(::slayerlog::debug_log::Severity::Info, message)

#define SLAYERLOG_LOG_DEBUG(message) SLAYERLOG_LOG_WITH_SEVERITY(::slayerlog::debug_log::Severity::Debug, message)

#define SLAYERLOG_LOG_TRACE(message) SLAYERLOG_LOG_WITH_SEVERITY(::slayerlog::debug_log::Severity::Trace, message)
