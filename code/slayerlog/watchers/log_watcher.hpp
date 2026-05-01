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
};

} // namespace slayerlog
