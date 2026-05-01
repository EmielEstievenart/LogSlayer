#pragma once

#include <memory>

#include "log_source.hpp"

namespace slayerlog
{

class LogWatcherBase;

std::unique_ptr<LogWatcherBase> create_log_watcher_for_source(const LogSource& source);

} // namespace slayerlog
