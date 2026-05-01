#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
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

private:
    static std::vector<std::string> build_ssh_arguments(const LogSource& source, std::uintmax_t offset);
    static std::string quote_for_posix_shell(std::string_view text);

    LogSource _source;
    StreamLineBuffer _line_buffer;
    std::uintmax_t _offset = 0;
    std::unique_ptr<ProcessPipe> _pipe;
    std::chrono::steady_clock::time_point _next_retry_at = std::chrono::steady_clock::time_point::min();
};

} // namespace slayerlog
