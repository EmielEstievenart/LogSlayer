#pragma once

#include <string>

#include "log_line.hpp"

namespace slayerlog
{

/// The label of the entry's owning source. Resolved live through metadata.source (labels
/// are deduplicated basenames that can change as sources open/close), falling back to the
/// per-entry copy for entries without a source object (manually built or transient entries).
const std::string& entry_source_label(const LogEntry& entry);

/// The user-visible prefix for a log entry's owning source.
/// Returns an empty string when the entry has no source, the mnemonic is hidden, or it is empty.
const std::string& presented_prefix(const LogEntry& entry);

/// The user-visible form of a log entry: source mnemonic prefix followed by raw text.
std::string presented_text(const LogEntry& entry);

/// The entry's raw text with its extracted timestamp removed. Falls back to the
/// full text when no extracted timestamp range is recorded. Used both for
/// deduplication keys and for the default (aligned-time) message rendering.
std::string message_text_without_extracted_timestamp(const LogEntry& entry);

/// Column widths shared by every rendered row so timestamps and messages line up.
struct LogEntryColumnWidths
{
    int line_number_width = 1;
    int timestamp_width   = 0;
};

/// The "{<utc timestamp>}" field for an entry's effective timestamp, or empty when it has none.
std::string render_log_entry_timestamp_field(const LogEntry& entry);

/// The display width occupied by render_log_entry_timestamp_field(), or 0 when the entry has no timestamp.
int log_entry_timestamp_field_width(const LogEntry& entry);

/// The message portion of a rendered row: full presented text when @p show_original_time,
/// otherwise the source prefix followed by the de-timestamped message.
std::string render_log_entry_message(const LogEntry& entry, bool show_original_time);

/// A full rendered row: right-justified @p line_number, padded timestamp field, then the message.
/// Hidden-column clipping is the caller's responsibility.
std::string render_log_entry_line(const LogEntry& entry, int line_number, LogEntryColumnWidths widths, bool show_original_time);

} // namespace slayerlog
