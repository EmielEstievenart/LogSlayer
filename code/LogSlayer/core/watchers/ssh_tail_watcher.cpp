#include "ssh_tail_watcher.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace slayerlog
{

namespace
{

constexpr auto reconnect_delay         = std::chrono::seconds(1);
constexpr std::size_t read_buffer_size = 4096;

// Bounds the bytes drained from the pipe per poll so callers polling under a lock stay
// responsive; whatever the remote keeps sending waits in the OS pipe until the next poll.
constexpr std::size_t max_drain_bytes_per_poll = 4 * 1024 * 1024;

std::string build_disconnect_message(const LogSource& source)
{
    return "[slayerlog] remote stream disconnected: " + source_display_path(source) + "; retrying";
}

std::size_t marker_prefix_overlap(std::string_view text, std::string_view marker)
{
    if (text.empty() || marker.empty())
    {
        return 0;
    }

    const std::size_t max_length = std::min(text.size(), marker.size() - 1);
    for (std::size_t length = max_length; length > 0; --length)
    {
        if (text.substr(text.size() - length, length) == marker.substr(0, length))
        {
            return length;
        }
    }

    return 0;
}

} // namespace

SshTailWatcher::SshTailWatcher(LogSource source) : _source(std::move(source))
{
    if (_source.kind != LogSourceKind::SshRemoteFile)
    {
        throw std::invalid_argument("SshTailWatcher requires an ssh remote source");
    }
}

bool SshTailWatcher::backlog_pending_locked() const
{
    return _backlog_pending;
}

bool SshTailWatcher::poll_locked(std::vector<std::string>& lines)
{
    _backlog_pending = false;

    const auto now = std::chrono::steady_clock::now();
    if (_pipe == nullptr && now < _next_retry_at)
    {
        return false;
    }

    if (_pipe == nullptr)
    {
        ++_connection_sequence;

        _ready_marker = make_stream_marker("READY");
        _reset_marker = make_stream_marker("RESET");
        _stdout_filter_buffer.clear();
        _awaiting_ready_marker = true;

        _pipe = std::make_unique<ProcessPipe>("ssh", build_ssh_arguments(_source, _offset, _ready_marker, _reset_marker));
    }

    std::array<char, read_buffer_size> buffer {};
    bool stdout_ended = false;
    bool stderr_ended = false;

    std::string stderr_text;
    std::size_t drained_stdout_bytes = 0;

    while (true)
    {
        bool made_progress = false;

        const std::size_t stdout_bytes = _pipe->read_stdout(buffer.data(), buffer.size(), stdout_ended);
        if (stdout_bytes > 0)
        {
            made_progress = true;
            drained_stdout_bytes += stdout_bytes;
            consume_stdout(std::string_view(buffer.data(), stdout_bytes), lines);
        }

        const std::size_t stderr_bytes = _pipe->read_stderr(buffer.data(), buffer.size(), stderr_ended);
        if (stderr_bytes > 0)
        {
            made_progress = true;
            stderr_text.append(buffer.data(), stderr_bytes);
        }

        if (stdout_ended || !_pipe->running())
        {
            flush_stdout_filter(lines);

            const bool had_stdout_output = !lines.empty();
            const int exit_code          = _pipe->wait();

            _pipe.reset();
            _stdout_filter_buffer.clear();
            _awaiting_ready_marker = false;
            _ready_marker.clear();
            _reset_marker.clear();
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

        if (drained_stdout_bytes >= max_drain_bytes_per_poll)
        {
            // The pipe may hold more; report it as backlog so the caller re-polls
            // promptly instead of blocking here for the whole burst.
            _backlog_pending = true;
            break;
        }
    }

    return !lines.empty();
}

std::string SshTailWatcher::make_stream_marker(std::string_view kind) const
{
    std::string marker = "__SLAYERLOG_SSH_";
    marker.append(kind.data(), kind.size());
    marker.push_back('_');
    marker.append(std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    marker.push_back('_');
    marker.append(std::to_string(_connection_sequence));
    marker.append("__");
    return marker;
}

void SshTailWatcher::append_log_bytes(std::string_view bytes, std::vector<std::string>& lines)
{
    if (bytes.empty())
    {
        return;
    }

    _offset += _line_buffer.append(bytes, lines);
}

void SshTailWatcher::consume_stdout(std::string_view text, std::vector<std::string>& lines)
{
    _stdout_filter_buffer.append(text.data(), text.size());

    if (_awaiting_ready_marker)
    {
        const auto marker_position = _stdout_filter_buffer.find(_ready_marker);
        if (marker_position == std::string::npos)
        {
            const std::size_t keep = _ready_marker.empty() ? 0 : std::min(_stdout_filter_buffer.size(), _ready_marker.size() - 1);

            if (_stdout_filter_buffer.size() > keep)
            {
                _stdout_filter_buffer.erase(0, _stdout_filter_buffer.size() - keep);
            }

            return;
        }

        _stdout_filter_buffer.erase(0, marker_position + _ready_marker.size());
        _awaiting_ready_marker = false;
    }

    if (_reset_marker.empty())
    {
        append_log_bytes(std::string_view(_stdout_filter_buffer.data(), _stdout_filter_buffer.size()), lines);
        _stdout_filter_buffer.clear();
        return;
    }

    while (!_stdout_filter_buffer.empty())
    {
        const auto marker_position = _stdout_filter_buffer.find(_reset_marker);
        if (marker_position != std::string::npos)
        {
            append_log_bytes(std::string_view(_stdout_filter_buffer.data(), marker_position), lines);

            _stdout_filter_buffer.erase(0, marker_position + _reset_marker.size());

            // The remote side has switched to a new file identity or detected truncation.
            // Do not allow an unterminated line from the old file to merge with the new file.
            _line_buffer.discard_pending_fragment();
            _offset = 0;
            continue;
        }

        const std::size_t keep      = marker_prefix_overlap(_stdout_filter_buffer, _reset_marker);
        const std::size_t emit_size = _stdout_filter_buffer.size() - keep;

        if (emit_size == 0)
        {
            break;
        }

        append_log_bytes(std::string_view(_stdout_filter_buffer.data(), emit_size), lines);
        _stdout_filter_buffer.erase(0, emit_size);
        break;
    }
}

void SshTailWatcher::flush_stdout_filter(std::vector<std::string>& lines)
{
    if (!_awaiting_ready_marker && !_stdout_filter_buffer.empty())
    {
        append_log_bytes(std::string_view(_stdout_filter_buffer.data(), _stdout_filter_buffer.size()), lines);
    }

    _stdout_filter_buffer.clear();
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

std::vector<std::string> SshTailWatcher::build_ssh_arguments(const LogSource& source, std::uintmax_t offset, std::string_view ready_marker, std::string_view reset_marker)
{
    const std::uintmax_t start_byte = offset + 1;

    const std::string quoted_path         = quote_for_posix_shell(source.remote_path);
    const std::string quoted_ready_marker = quote_for_posix_shell(ready_marker);
    const std::string quoted_reset_marker = quote_for_posix_shell(reset_marker);

    std::ostringstream remote_script;

    remote_script << "path=" << quoted_path << "; "
                  << "ready=" << quoted_ready_marker << "; "
                  << "reset=" << quoted_reset_marker
                  << "; "

                  // Marker is emitted without a newline so filtering it never changes the
                  // first byte of the real log stream.
                  << "printf '%s' \"$ready\"; "

                  << "file_size() { "
                  << "value=$(wc -c < \"$path\" 2>/dev/null) || return 1; "
                  << "set -- $value; "
                  << "[ $# -gt 0 ] || return 1; "
                  << "printf '%s\\n' \"$1\"; "
                  << "}; "

                  // Prefer stat because it gives device + inode. Fall back to ls -di for
                  // smaller or older systems where stat is missing or has different flags.
                  << "file_id() { "
                  << "value=$(stat -c '%d:%i' -- \"$path\" 2>/dev/null) || "
                  << "value=$(stat -f '%d:%i' \"$path\" 2>/dev/null) || "
                  << "value=$(ls -di -- \"$path\" 2>/dev/null) || return 1; "
                  << "set -- $value; "
                  << "[ $# -gt 0 ] || return 1; "
                  << "printf '%s\\n' \"$1\"; "
                  << "}; "

                  << "emit_reset() { printf '%s' \"$reset\"; }; "

                  << "start=" << start_byte
                  << "; "

                  // Keep the old contract for an initially missing or unreadable file:
                  // fail this SSH command and let the local reconnect path report/retry it.
                  << "size=$(file_size) || exit 1; "

                  // Emulate follow-by-name instead of relying on tail -F. This is more
                  // portable on systems where tail -F is missing or does not notice
                  // backup/recreate rotation.
                  << "while :; do "
                  << "size=$(file_size) || { emit_reset; start=1; sleep 1; continue; }; "

                  // start == size + 1 is valid: it means the local side is exactly at EOF.
                  // Only reset when the requested start is beyond EOF by more than one byte.
                  << "if [ \"$start\" -gt \"$((size + 1))\" ]; then emit_reset; start=1; fi; "

                  << "id=$(file_id) || { emit_reset; start=1; sleep 1; continue; }; "

                  << "tail -c +\"$start\" -f -- \"$path\" 2>/dev/null & "
                  << "tail_pid=$!; "

                  << "while kill -0 \"$tail_pid\" 2>/dev/null; do "
                  << "sleep 1; "

                  << "new_size=$(file_size) || { "
                  << "kill \"$tail_pid\" 2>/dev/null; "
                  << "wait \"$tail_pid\" 2>/dev/null; "
                  << "emit_reset; "
                  << "start=1; "
                  << "break; "
                  << "}; "

                  << "new_id=$(file_id) || { "
                  << "kill \"$tail_pid\" 2>/dev/null; "
                  << "wait \"$tail_pid\" 2>/dev/null; "
                  << "emit_reset; "
                  << "start=1; "
                  << "break; "
                  << "}; "

                  // Reopen when the path now refers to a different file, or when the same
                  // file was truncated. This covers rename+recreate and copy/truncate
                  // style rotations.
                  << "if [ \"$new_id\" != \"$id\" ] || [ \"$new_size\" -lt \"$size\" ]; then "
                  << "kill \"$tail_pid\" 2>/dev/null; "
                  << "wait \"$tail_pid\" 2>/dev/null; "
                  << "emit_reset; "
                  << "start=1; "
                  << "break; "
                  << "fi; "

                  << "size=\"$new_size\"; "
                  << "done; "

                  << "wait \"$tail_pid\" 2>/dev/null; "

                  // If tail died without a rotation reset, resume from current EOF to
                  // avoid replaying old content.
                  << "if [ \"$start\" != 1 ]; then size=$(file_size) && start=$((size + 1)); fi; "
                  << "done";

    const std::string remote_command = "sh -c " + quote_for_posix_shell(remote_script.str());

    return {
        // This watcher is read-only; prevent ssh from stealing terminal input from the UI.
        "-n", "-T", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3", source.ssh_target, remote_command,
    };
}

} // namespace slayerlog