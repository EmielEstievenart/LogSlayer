#include "implementations/log_view1/close_open_file_command.hpp"

#include <cctype>
#include <string>
#include <utility>

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

CloseOpenFileCommand::CloseOpenFileCommand(CommandContext context, CommandPaletteSession& palette_session) : _context(context), _palette_session(palette_session)
{
}

const CommandDescriptor& CloseOpenFileCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"close-open-file", "Close one currently open file", "close-open-file", {"Opens a picker containing the currently tracked sources.", "Use Up/Down to select a source and Enter to close it."}};
    return descriptor;
}

CommandResult CloseOpenFileCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: close-open-file"};
    }

    auto labels = _context.tracked_sources.source_display_labels();
    if (labels.empty())
    {
        return {false, "No open files to close"};
    }

    _palette_session.open_close_open_file_picker(std::move(labels),
                                                 [this](std::size_t selected_index) -> CommandResult
                                                 {
                                                     std::string closed_label;
                                                     const auto error = _context.tracked_sources.close_source(selected_index, &closed_label);
                                                     if (error.has_value())
                                                     {
                                                         SLAYERLOG_LOG_ERROR("close-open-file failed selected_index=" << selected_index << " error=" << *error);
                                                         return {false, *error, false};
                                                     }

                                                     _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
                                                     return {true, "Closed file: " + closed_label};
                                                 });

    return {true, "Select a file to close", false};
}

} // namespace slayerlog
