#include "implementations/log_view1/set_time_format_command.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "command_palette_session.hpp"
#include "debug_log.hpp"
#include "log_view_service.hpp"
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

SetTimeFormatCommand::SetTimeFormatCommand(CommandContext context, CommandPaletteSession& palette_session) : _context(context), _palette_session(palette_session)
{
}

const CommandDescriptor& SetTimeFormatCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"set-time-format",
                                               "Set timestamp parser for one source",
                                               "set-time-format",
                                               {"Opens a source picker, then a timestamp format picker.", "Confirm each selection with Enter. All lines from that source are reparsed and all tracked lines are re-sorted."}};
    return descriptor;
}

CommandResult SetTimeFormatCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: set-time-format"};
    }

    std::vector<std::string> labels = _context.tracked_sources.source_display_labels();
    if (labels.empty())
    {
        return {false, "No open sources to configure"};
    }

    std::vector<std::string> formats = _context.tracked_sources.timestamp_formats();
    if (formats.empty())
    {
        return {false, "No timestamp formats configured"};
    }

    _palette_session.open_timestamp_source_picker(labels,
                                                  [this, labels, formats](std::size_t source_index) -> CommandResult
                                                  {
                                                      if (source_index >= labels.size())
                                                      {
                                                          return {false, "Invalid source selection", false};
                                                      }

                                                      _palette_session.open_timestamp_format_picker(formats,
                                                                                                    [this, labels, formats, source_index](std::size_t format_index) -> CommandResult
                                                                                                    {
                                                                                                        if (format_index >= formats.size())
                                                                                                        {
                                                                                                            return {false, "Invalid timestamp format selection", false};
                                                                                                        }

                                                                                                        const auto error = _context.tracked_sources.set_source_timestamp_format(source_index, formats[format_index]);
                                                                                                        if (error.has_value())
                                                                                                        {
                                                                                                            SLAYERLOG_LOG_ERROR("set-time-format failed source_index=" << source_index << " format_index=" << format_index
                                                                                                                                                                       << " error=" << *error);
                                                                                                            return {false, *error, false};
                                                                                                        }

                                                                                                        _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
                                                                                                        return {true, "Set timestamp format for " + labels[source_index] + ": " + formats[format_index]};
                                                                                                    });

                                                      return {true, "Select timestamp format for " + labels[source_index], false};
                                                  });

    return {true, "Select a source to configure", false};
}

} // namespace slayerlog
