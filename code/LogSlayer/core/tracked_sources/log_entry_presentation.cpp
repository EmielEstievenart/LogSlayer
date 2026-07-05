#include "log_entry_presentation.hpp"

#include <iomanip>
#include <sstream>

#include "tracked_source_base.hpp"

namespace slayerlog
{

const std::string& entry_source_label(const LogEntry& entry)
{
    if (entry.metadata.source != nullptr)
    {
        return entry.metadata.source->source_label();
    }

    return entry.metadata.source_label;
}

const std::string& presented_prefix(const LogEntry& entry)
{
    // metadata.source can be null for manually-built tests or transient entries.
    static const std::string no_prefix;
    if (entry.metadata.source == nullptr)
    {
        return no_prefix;
    }

    return entry.metadata.source->mnemonic_prefix();
}

std::string presented_text(const LogEntry& entry)
{
    return presented_prefix(entry) + entry.text;
}

std::string message_text_without_extracted_timestamp(const LogEntry& entry)
{
    if (!entry.metadata.extracted_time_start.has_value() || !entry.metadata.extracted_time_end.has_value())
    {
        return entry.text;
    }

    const std::size_t start = *entry.metadata.extracted_time_start;
    const std::size_t end   = *entry.metadata.extracted_time_end;
    if (start >= end || end > entry.text.size())
    {
        return entry.text;
    }

    std::string trimmed_text = entry.text;
    trimmed_text.erase(start, end - start);
    return trimmed_text;
}

std::string render_log_entry_timestamp_field(const LogEntry& entry)
{
    const auto timestamp = effective_timestamp(entry.metadata);
    if (!timestamp.has_value())
    {
        return {};
    }

    return "{" + format_log_timestamp_utc(*timestamp) + "}";
}

int log_entry_timestamp_field_width(const LogEntry& entry)
{
    const auto timestamp = effective_timestamp(entry.metadata);
    if (!timestamp.has_value())
    {
        return 0;
    }

    return static_cast<int>(format_log_timestamp_utc(*timestamp).size()) + 2;
}

std::string render_log_entry_message(const LogEntry& entry, bool show_original_time)
{
    if (show_original_time)
    {
        return presented_text(entry);
    }

    return presented_prefix(entry) + message_text_without_extracted_timestamp(entry);
}

std::string render_log_entry_line(const LogEntry& entry, int line_number, LogEntryColumnWidths widths, bool show_original_time)
{
    std::ostringstream output;
    output << std::setw(widths.line_number_width) << std::right << line_number << " ";
    if (widths.timestamp_width > 0)
    {
        output << std::left << std::setw(widths.timestamp_width) << render_log_entry_timestamp_field(entry) << std::right << " ";
    }

    output << render_log_entry_message(entry, show_original_time);
    return output.str();
}

} // namespace slayerlog
