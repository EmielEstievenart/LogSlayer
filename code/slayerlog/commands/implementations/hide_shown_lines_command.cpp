#include "implementations/hide_shown_lines_command.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

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

std::optional<int> highest_shown_line_number(const AllProcessedSources& processed_sources, const LogController& controller)
{
    if (processed_sources.line_count() == 0) { return std::nullopt; }
    const int viewport_line_count = std::max(1, controller.text_view_controller().viewport_line_count());
    const int first_visible_line = controller.text_view_controller().first_visible_line();
    const int last_visible_line = std::min(processed_sources.line_count() - 1, first_visible_line + viewport_line_count - 1);
    return processed_sources.line_number_for_visible_line(VisibleLineIndex {last_visible_line});
}
}

HideShownLinesCommand::HideShownLinesCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& HideShownLinesCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"hide-shown-lines", "Hide all currently shown lines", "hide-shown-lines"};
    return descriptor;
}

CommandResult HideShownLinesCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: hide-shown-lines"}; }
    const auto line_number = highest_shown_line_number(_context.processed_sources, _context.log_controller);
    if (!line_number.has_value()) { return {false, "No lines are currently shown"}; }
    _context.processed_sources.hide_before_line_number(*line_number + 1);
    _context.log_controller.rebuild_view(_context.processed_sources);
    return {true, "Hidden all currently shown lines"};
}

} // namespace slayerlog
