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

protected:
    virtual bool poll_locked(std::vector<std::string>& lines) = 0;

private:
    std::mutex _mutex;
};

} // namespace slayerlog
