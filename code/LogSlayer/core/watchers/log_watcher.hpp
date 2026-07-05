#pragma once

#include <string>
#include <vector>

namespace slayerlog
{

class LogWatcher
{
public:
    virtual ~LogWatcher() = default;

    virtual bool poll(std::vector<std::string>& lines) = 0;

    /**
     * Whether the transport has more data immediately available that the last poll()
     * intentionally left behind (watchers bound the bytes ingested per poll so callers
     * never hold locks across an unbounded read). Callers should poll again promptly
     * while this is true instead of waiting a full poll interval.
     */
    virtual bool backlog_pending() const { return false; }
};

} // namespace slayerlog
