#include "implementations/log_view1/align_time_command.hpp"

#include <cctype>
#include <string>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_controller.hpp"
#include "notifications/notification.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{
namespace
{

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) { ++start; }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) { --end; }
    return std::string(text.substr(start, end - start));
}

void show_alignment_notification(const Notifier& notifier, const std::string& source_label, LogTimestampOffset offset)
{
    Notification notification;
    notification.title   = "Time aligned";
    notification.message = source_label + " offset " + format_log_timestamp_offset(offset);
    notification.level   = NotificationLevel::Success;
    (void)notifier.show(std::move(notification));
}

} // namespace

AlignTimeCommand::AlignTimeCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& AlignTimeCommand::descriptor() const
{
    static const CommandDescriptor descriptor {
        "align-time",
        "Align one source to another selected log line",
        "align-time",
        {"Closes the command palette and enables a selector in the main log view.", "Press Enter on the source line, then Enter on the destination line.", "The source uses its original timestamp; the destination uses its effective timestamp.", "Use Ctrl+F, PageUp, and PageDown while selecting."},
    };
    return descriptor;
}

CommandResult AlignTimeCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: align-time"}; }
    if (_context.tracked_sources.source_count() < 2) { return {false, "At least two open sources are required"}; }
    if (_context.processed_sources.line_count() == 0) { return {false, "No visible log lines to align"}; }

    _context.log_controller.time_alignment_controller().start(
        _context.log_controller.text_view_controller().first_visible_line(),
        [context = _context](const LogEntry& source_entry, const LogEntry& destination_entry) mutable
        {
            if (!source_entry.metadata.timestamp.has_value())
            {
                return TimeAlignmentApplyResult {false, "Source line has no original timestamp"};
            }

            const auto destination_timestamp = effective_timestamp(destination_entry.metadata);
            if (!destination_timestamp.has_value())
            {
                return TimeAlignmentApplyResult {false, "Destination line has no timestamp"};
            }

            const auto offset = offset_between(*source_entry.metadata.timestamp, *destination_timestamp);
            if (!offset.has_value())
            {
                return TimeAlignmentApplyResult {false, "Timestamp offset would overflow"};
            }

            const std::size_t source_index = source_entry.metadata.source_index;
            const std::string source_label = source_entry.metadata.source_label;
            const auto error = context.tracked_sources.set_source_timestamp_offset(source_index, *offset);
            if (error.has_value())
            {
                SLAYERLOG_LOG_ERROR("align-time failed source_index=" << source_index << " error=" << *error);
                return TimeAlignmentApplyResult {false, *error};
            }

            reload_processed_sources(context.tracked_sources, context.header_text, context.processed_sources, context.log_controller, context.screen);
            show_alignment_notification(context.notifier, source_label, *offset);
            return TimeAlignmentApplyResult {true, "Aligned " + source_label + " by " + format_log_timestamp_offset(*offset)};
        });

    return {true, "Select source line", true};
}

} // namespace slayerlog
