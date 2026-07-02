#include "implementations/log_view1/adjust_time_offset_command.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "command_palette_session.hpp"
#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_view_service.hpp"
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
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return std::string(text.substr(start, end - start));
}
}

AdjustTimeOffsetCommand::AdjustTimeOffsetCommand(CommandContext context, CommandPaletteSession& palette_session) : _context(context), _palette_session(palette_session)
{
}

const CommandDescriptor& AdjustTimeOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"adjust-time-offset",
                                               "Add a timestamp offset to one source",
                                               "adjust-time-offset",
                                               {"Opens a source picker, then an offset input prompt.", "The entered offset is added on top of the source's current offset; use clear-time-offset to reset.",
                                                "Offset format is DD hh:mm:ss[.fraction], with an optional leading + or - sign.", "Example: adjust-time-offset, then enter -00 00:00:10.005"}};
    return descriptor;
}

CommandResult AdjustTimeOffsetCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: adjust-time-offset"};
    }

    std::vector<std::string> labels = _context.tracked_sources.source_display_labels();
    if (labels.empty())
    {
        return {false, "No open sources to configure"};
    }

    std::vector<std::string> picker_labels = source_labels_with_offsets(_context.tracked_sources);
    _palette_session.open_timestamp_source_picker(picker_labels,
                                                  [this, labels, picker_labels](std::size_t source_index) -> CommandResult
                                                  {
                                                      if (source_index >= labels.size())
                                                      {
                                                          return {false, "Invalid source selection", false};
                                                      }

                                                      _palette_session.open_timestamp_offset_input(picker_labels[source_index],
                                                                                                   [this, labels, source_index](std::string_view offset_text) -> CommandResult
                                                                                                   {
                                                                                                       const auto offset = parse_log_timestamp_offset(offset_text);
                                                                                                       if (!offset.has_value())
                                                                                                       {
                                                                                                           return {false, "Invalid offset: expected DD hh:mm:ss[.fraction]", false};
                                                                                                       }

                                                                                                       const auto error = _context.tracked_sources.adjust_source_timestamp_offset(source_index, *offset);
                                                                                                       if (error.has_value())
                                                                                                       {
                                                                                                           SLAYERLOG_LOG_ERROR("adjust-time-offset failed source_index=" << source_index << " error=" << *error);
                                                                                                           return {false, *error, false};
                                                                                                       }

                                                                                                       _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
                                                                                                       const auto total = _context.tracked_sources.source_timestamp_offset(source_index);
                                                                                                       return {true, "Adjusted time offset for " + labels[source_index] + " by " + format_log_timestamp_offset(*offset) + " to " +
                                                                                                                         format_log_timestamp_offset(total.value_or(LogTimestampOffset {}))};
                                                                                                   });

                                                      return {true, "Enter timestamp offset for " + labels[source_index], false};
                                                  });

    return {true, "Select a source to offset", false};
}

} // namespace slayerlog
