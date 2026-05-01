#include "ssh_tail_watcher.hpp"

#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <thread>

namespace slayerlog
{

namespace
{

constexpr auto reconnect_delay         = std::chrono::seconds(1);
constexpr std::size_t read_buffer_size = 4096;

std::string build_disconnect_message(const LogSource& source)
{
    return "[slayerlog] remote stream disconnected: " + source_display_path(source) + "; retrying";
}

} // namespace

SshTailWatcher::SshTailWatcher(LogSource source) : _source(std::move(source))
{
    if (_source.kind != LogSourceKind::SshRemoteFile)
    {
        throw std::invalid_argument("SshTailWatcher requires an ssh remote source");
    }
}

bool SshTailWatcher::poll_locked(std::vector<std::string>& lines)
{
    const auto now = std::chrono::steady_clock::now();
    if (_pipe == nullptr && now < _next_retry_at)
    {
        return false;
    }

    if (_pipe == nullptr)
    {
        _pipe = std::make_unique<ProcessPipe>("ssh", build_ssh_arguments(_source, _offset));
    }

    std::array<char, read_buffer_size> buffer {};
    bool stdout_ended = false;
    bool stderr_ended = false;

    std::string stderr_text;

    while (true)
    {
        bool made_progress = false;

        const std::size_t stdout_bytes = _pipe->read_stdout(buffer.data(), buffer.size(), stdout_ended);
        if (stdout_bytes > 0)
        {
            made_progress = true;
            _offset += _line_buffer.append(std::string_view(buffer.data(), stdout_bytes), lines);
        }

        const std::size_t stderr_bytes = _pipe->read_stderr(buffer.data(), buffer.size(), stderr_ended);
        if (stderr_bytes > 0)
        {
            made_progress = true;
            stderr_text.append(buffer.data(), stderr_bytes);
        }

        if (stdout_ended || !_pipe->running())
        {
            const bool had_stdout_output = !lines.empty();
            const int exit_code          = _pipe->wait();
            _pipe.reset();
            _line_buffer.discard_pending_fragment();
            _next_retry_at = std::chrono::steady_clock::now() + reconnect_delay;

            if (!stderr_text.empty())
            {
                std::istringstream stderr_stream(stderr_text);
                std::string line;
                while (std::getline(stderr_stream, line))
                {
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }

                    if (!line.empty())
                    {
                        lines.push_back("[slayerlog] ssh stderr: " + line);
                    }
                }
            }

            if (!had_stdout_output && exit_code != 0 && lines.empty())
            {
                lines.push_back("[slayerlog] failed to connect to remote stream: " + source_display_path(_source));
            }

            lines.push_back(build_disconnect_message(_source));
            break;
        }

        if (!made_progress)
        {
            break;
        }
    }

    return !lines.empty();
}

std::string SshTailWatcher::quote_for_posix_shell(std::string_view text)
{
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('\'');
    for (const char character : text)
    {
        if (character == '\'')
        {
            quoted.append("'\"'\"'");
        }
        else
        {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::vector<std::string> SshTailWatcher::build_ssh_arguments(const LogSource& source, std::uintmax_t offset)
{
    const std::uintmax_t start_byte = offset + 1;
    const std::string quoted_path   = quote_for_posix_shell(source.remote_path);

    std::ostringstream remote_script;
    remote_script << "size=$(wc -c < " << quoted_path << " 2>/dev/null) || exit 1; "
                  << "start=" << start_byte << "; "
                  << "if [ \"$size\" -lt \"$start\" ]; then start=1; fi; "
                  << "tail -c +\"$start\" -F -- " << quoted_path << " 2>/dev/null || "
                  << "exec tail -c +\"$start\" -f -- " << quoted_path;

    return {
        // This watcher is read-only; prevent ssh from stealing terminal input from the UI.
        "-n", "-T", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3", source.ssh_target, "sh", "-lc", remote_script.str(),
    };
}

} // namespace slayerlog
