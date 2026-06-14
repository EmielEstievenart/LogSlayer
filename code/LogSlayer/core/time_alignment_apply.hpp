#pragma once

#include <functional>
#include <string>

#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

/// Outcome of applying a time-alignment selection (source entry -> destination
/// entry). UI-agnostic so the alignment contract can live in the core library.
struct TimeAlignmentApplyResult
{
    bool success = false;
    std::string message;
};

using TimeAlignmentApplyCallback = std::function<TimeAlignmentApplyResult(const LogEntry& source_entry, const LogEntry& destination_entry)>;

} // namespace slayerlog
