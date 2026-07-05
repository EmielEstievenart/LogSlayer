#pragma once

#include <mutex>
#include <vector>

#include "watchers/log_watcher.hpp"

namespace slayerlog
{

class LogWatcherBase : public LogWatcher
{
public:
    bool poll(std::vector<std::string>& lines) final
    {
        lines.clear();
        std::lock_guard lock(_mutex);
        return poll_locked(lines);
    }

    bool backlog_pending() const final
    {
        std::lock_guard lock(_mutex);
        return backlog_pending_locked();
    }

protected:
    virtual bool poll_locked(std::vector<std::string>& lines) = 0;

    /// Runs under the watcher mutex; implementations report leftover data here.
    virtual bool backlog_pending_locked() const { return false; }

private:
    mutable std::mutex _mutex;
};

} // namespace slayerlog
