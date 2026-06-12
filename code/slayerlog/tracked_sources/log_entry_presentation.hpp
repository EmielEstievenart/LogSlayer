#pragma once

#include <string>

#include "log_line.hpp"

namespace slayerlog
{

/// The user-visible prefix for a log entry's owning source.
/// Returns an empty string when the entry has no source, the mnemonic is hidden, or it is empty.
std::string presented_prefix(const LogEntry& entry);

/// The user-visible form of a log entry: source mnemonic prefix followed by raw text.
std::string presented_text(const LogEntry& entry);

} // namespace slayerlog
