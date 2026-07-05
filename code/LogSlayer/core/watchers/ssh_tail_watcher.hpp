#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "log_source.hpp"
#include "log_watcher_base.hpp"
#include "process_pipe.hpp"
#include "stream_line_buffer.hpp"

namespace slayerlog
{

class SshTailWatcher : public LogWatcherBase
{
public:
    explicit SshTailWatcher(LogSource source);

protected:
    bool poll_locked(std::vector<std::string>& lines) override;
    bool backlog_pending_locked() const override;

private:
    static std::vector<std::string> build_ssh_arguments(const LogSource& source, std::uintmax_t offset, std::string_view ready_marker, std::string_view reset_marker);

    static std::string quote_for_posix_shell(std::string_view text);

    std::string make_stream_marker(std::string_view kind) const;

    void consume_stdout(std::string_view text, std::vector<std::string>& lines);
    void flush_stdout_filter(std::vector<std::string>& lines);
    void append_log_bytes(std::string_view bytes, std::vector<std::string>& lines);

    LogSource _source;
    StreamLineBuffer _line_buffer;
    std::uintmax_t _offset = 0;
    std::unique_ptr<ProcessPipe> _pipe;
    std::chrono::steady_clock::time_point _next_retry_at = std::chrono::steady_clock::time_point::min();

    std::uint64_t _connection_sequence = 0;
    std::string _ready_marker;
    std::string _reset_marker;
    std::string _stdout_filter_buffer;
    bool _awaiting_ready_marker = false;
    bool _backlog_pending       = false;
};

} // namespace slayerlog