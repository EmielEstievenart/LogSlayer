#include "implementations/log_view1/clear_time_offset_command.hpp"

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

ClearTimeOffsetCommand::ClearTimeOffsetCommand(CommandContext context, CommandPaletteSession& palette_session) : _context(context), _palette_session(palette_session)
{
}

const CommandDescriptor& ClearTimeOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"clear-time-offset", "Clear timestamp offset for one source", "clear-time-offset", {"Opens a source picker and clears that source's active timestamp offset."}};
    return descriptor;
}

CommandResult ClearTimeOffsetCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: clear-time-offset"};
    }

    std::vector<std::string> labels = _context.tracked_sources.source_display_labels();
    if (labels.empty())
    {
        return {false, "No open sources to configure"};
    }

    _palette_session.open_timestamp_source_picker(labels,
                                                  [this, labels](std::size_t source_index) -> CommandResult
                                                  {
                                                      if (source_index >= labels.size())
                                                      {
                                                          return {false, "Invalid source selection", false};
                                                      }

                                                      const auto error = _context.tracked_sources.clear_source_timestamp_offset(source_index);
                                                      if (error.has_value())
                                                      {
                                                          SLAYERLOG_LOG_ERROR("clear-time-offset failed source_index=" << source_index << " error=" << *error);
                                                          return {false, *error, false};
                                                      }

                                                      _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
                                                      return {true, "Cleared time offset for " + labels[source_index]};
                                                  });

    return {true, "Select a source to clear offset", false};
}

} // namespace slayerlog
