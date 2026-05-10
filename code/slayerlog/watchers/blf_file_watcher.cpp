#include "blf_file_watcher.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include "debug_log.hpp"
#include "process_pipe.hpp"

namespace slayerlog
{

namespace
{

constexpr std::size_t read_buffer_size = 4096;

struct PythonCommand
{
    std::string executable;
    std::vector<std::string> prefix_arguments;
};

std::string escape_json_string(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (const char value : text)
    {
        const auto character = static_cast<unsigned char>(value);
        switch (character)
        {
        case '\\':
            escaped.append("\\\\");
            break;
        case '"':
            escaped.append("\\\"");
            break;
        case '\b':
            escaped.append("\\b");
            break;
        case '\f':
            escaped.append("\\f");
            break;
        case '\n':
            escaped.append("\\n");
            break;
        case '\r':
            escaped.append("\\r");
            break;
        case '\t':
            escaped.append("\\t");
            break;
        default:
            if (character < 0x20)
            {
                constexpr char hex[] = "0123456789ABCDEF";
                escaped.append("\\u00");
                escaped.push_back(hex[(character >> 4) & 0x0F]);
                escaped.push_back(hex[character & 0x0F]);
            }
            else
            {
                escaped.push_back(value);
            }
            break;
        }
    }
    return escaped;
}

std::string trim_stderr(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '\t' || text.back() == ' '))
    {
        text.pop_back();
    }

    constexpr std::size_t max_stderr_length = 1000;
    if (text.size() > max_stderr_length)
    {
        text.resize(max_stderr_length);
        text.append("...");
    }

    return text;
}

std::string read_environment_variable(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
    {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::vector<PythonCommand> python_commands()
{
    std::vector<PythonCommand> commands;
    const std::string override_python = read_environment_variable("LOGSLAYER_PYTHON");
    if (!override_python.empty())
    {
        commands.push_back({override_python, {}});
        return commands;
    }

    commands.push_back({"python3", {}});
    commands.push_back({"python", {}});
#ifdef _WIN32
    commands.push_back({"py", {"-3"}});
#endif
    return commands;
}

bool python_not_found_result(const BlfFileWatcher::ImportResult& result)
{
    if (!result.stdout_lines.empty())
    {
        return false;
    }

    if (result.exit_code == 127)
    {
        return true;
    }

#ifdef _WIN32
    // Windows app-execution aliases for python/python3 can start successfully,
    // print the Store guidance, and exit with 9009. Treat that as not found so
    // we can continue to the next configured candidate, usually py -3.
    return result.exit_code == 9009;
#else
    return false;
#endif
}

std::vector<std::filesystem::path> importer_script_candidates()
{
    std::vector<std::filesystem::path> candidates;
    const std::string override_importer = read_environment_variable("LOGSLAYER_BLF_IMPORTER");
    if (!override_importer.empty())
    {
        candidates.emplace_back(override_importer);
        return candidates;
    }

    const auto& executable_dir = debug_log::runtime_paths().executable_dir;
    if (!executable_dir.empty())
    {
        candidates.push_back(executable_dir / "tools" / "importers" / "blf" / "blf_to_logslayer.py");
    }

    std::error_code error;
    const auto current_path = std::filesystem::current_path(error);
    if (!error)
    {
        candidates.push_back(current_path / "tools" / "importers" / "blf" / "blf_to_logslayer.py");
    }

#ifdef LOGSLAYER_BLF_IMPORTER_SOURCE_PATH
    candidates.emplace_back(LOGSLAYER_BLF_IMPORTER_SOURCE_PATH);
#endif

    return candidates;
}

std::optional<std::filesystem::path> find_importer_script()
{
    for (const auto& candidate : importer_script_candidates())
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
        {
            return candidate;
        }
    }

    return std::nullopt;
}

std::vector<std::string> split_output_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        const auto newline = text.find('\n', start);
        if (newline == std::string::npos)
        {
            std::string line = text.substr(start);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                lines.push_back(std::move(line));
            }
            break;
        }

        std::string line = text.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = newline + 1;
    }

    return lines;
}

BlfFileWatcher::ImportResult run_process(const PythonCommand& command, const std::filesystem::path& importer_script, const std::string& file_path)
{
    std::vector<std::string> arguments = command.prefix_arguments;
    arguments.push_back(importer_script.string());
    arguments.push_back(file_path);

    ProcessPipe pipe(command.executable, std::move(arguments));
    std::array<char, read_buffer_size> buffer {};
    std::string stdout_text;
    std::string stderr_text;

    while (true)
    {
        bool made_progress = false;
        bool stdout_ended = false;
        bool stderr_ended = false;

        const std::size_t stdout_bytes = pipe.read_stdout(buffer.data(), buffer.size(), stdout_ended);
        if (stdout_bytes > 0)
        {
            made_progress = true;
            stdout_text.append(buffer.data(), stdout_bytes);
        }

        const std::size_t stderr_bytes = pipe.read_stderr(buffer.data(), buffer.size(), stderr_ended);
        if (stderr_bytes > 0)
        {
            made_progress = true;
            stderr_text.append(buffer.data(), stderr_bytes);
        }

        if (!pipe.running())
        {
            while (true)
            {
                bool drain_stdout_ended = false;
                bool drain_stderr_ended = false;
                const std::size_t drain_stdout_bytes = pipe.read_stdout(buffer.data(), buffer.size(), drain_stdout_ended);
                if (drain_stdout_bytes > 0)
                {
                    stdout_text.append(buffer.data(), drain_stdout_bytes);
                }

                const std::size_t drain_stderr_bytes = pipe.read_stderr(buffer.data(), buffer.size(), drain_stderr_ended);
                if (drain_stderr_bytes > 0)
                {
                    stderr_text.append(buffer.data(), drain_stderr_bytes);
                }

                if (drain_stdout_bytes == 0 && drain_stderr_bytes == 0)
                {
                    break;
                }
            }

            BlfFileWatcher::ImportResult result;
            result.exit_code = pipe.wait();
            result.stdout_lines = split_output_lines(stdout_text);
            result.stderr_text = std::move(stderr_text);
            return result;
        }

        if (!made_progress)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

} // namespace

BlfFileWatcher::BlfFileWatcher(std::string file_path) : BlfFileWatcher(std::move(file_path), run_importer_process) { }

BlfFileWatcher::BlfFileWatcher(std::string file_path, ImportRunner import_runner) : _file_path(std::move(file_path)), _import_runner(std::move(import_runner))
{
    SLAYERLOG_LOG_INFO("Created BLF file watcher for file=" << _file_path);
}

bool BlfFileWatcher::poll_locked(std::vector<std::string>& lines)
{
    if (_consumed)
    {
        return false;
    }

    _consumed = true;
    ImportResult result;
    try
    {
        result = _import_runner(_file_path);
    }
    catch (const std::exception& ex)
    {
        result.started = false;
        result.error_message = ex.what();
    }

    lines = std::move(result.stdout_lines);

    if (!result.started)
    {
        lines.push_back(build_import_error_line(_file_path, result.error_message.empty() ? "Failed to start BLF importer" : result.error_message));
        return true;
    }

    if (result.exit_code != 0)
    {
        std::string message = "BLF importer exited with code " + std::to_string(result.exit_code);
        const std::string stderr_text = trim_stderr(std::move(result.stderr_text));
        if (!stderr_text.empty())
        {
            message.append(": ");
            message.append(stderr_text);
        }

        if (lines.empty())
        {
            lines.push_back(build_import_error_line(_file_path, message));
        }
        else
        {
            lines.push_back(build_import_error_line(_file_path, message));
        }
    }

    return !lines.empty();
}

BlfFileWatcher::ImportResult BlfFileWatcher::run_importer_process(const std::string& file_path)
{
    const auto importer_script = find_importer_script();
    if (!importer_script.has_value())
    {
        return {false, 0, {}, {}, "BLF importer script was not found. Set LOGSLAYER_BLF_IMPORTER or install tools/importers/blf/blf_to_logslayer.py next to LogSlayer."};
    }

    std::string last_error;
    for (const auto& command : python_commands())
    {
        try
        {
            auto result = run_process(command, *importer_script, file_path);
            if (python_not_found_result(result) && read_environment_variable("LOGSLAYER_PYTHON").empty())
            {
                last_error = "Failed to start Python interpreter: " + command.executable;
                continue;
            }

            return result;
        }
        catch (const std::exception& ex)
        {
            last_error = ex.what();
            if (!read_environment_variable("LOGSLAYER_PYTHON").empty())
            {
                break;
            }
        }
    }

    return {false, 0, {}, {}, last_error.empty() ? "Python interpreter was not found. Install Python or set LOGSLAYER_PYTHON." : last_error};
}

std::string BlfFileWatcher::build_import_error_line(const std::string& file_path, const std::string& message)
{
    std::ostringstream output;
    output << "{\"schema\":\"logslayer.blf.v1\",\"kind\":\"import_error\",\"ts\":null,\"message\":\"" << escape_json_string(message) << "\",\"source\":\"" << escape_json_string(file_path) << "\"}";
    return output.str();
}

} // namespace slayerlog
