#include "implementations/hide_before_line_command.hpp"

#include <exception>
#include <optional>
#include <string>

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{
namespace
{
std::optional<int> parse_positive_line_number(std::string_view text)
{
    if (text.empty()) { return std::nullopt; }
    const std::string line_text(text);
    std::size_t parsed_length = 0;
    int line_number = 0;
    try { line_number = std::stoi(line_text, &parsed_length); }
    catch (const std::exception&) { return std::nullopt; }
    if (parsed_length != line_text.size() || line_number <= 0) { return std::nullopt; }
    return line_number;
}
}

HideBeforeLineCommand::HideBeforeLineCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& HideBeforeLineCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"hide-before-line", "Hide all raw lines before a line number", "hide-before-line <line-number>", {"Use 1 to show everything again.", "Example: hide-before-line 5000"}};
    return descriptor;
}

CommandResult HideBeforeLineCommand::execute(std::string_view arguments)
{
    const auto line_number = parse_positive_line_number(arguments);
    if (!line_number.has_value()) { return {false, "Usage: hide-before-line <line-number>"}; }
    _context.processed_sources.hide_before_line_number(*line_number);
    _context.log_view.rebuild_view(_context.processed_sources);
    if (*line_number == 1) { return {true, "Showing all lines"}; }
    return {true, "Hidden all lines before line " + std::to_string(*line_number)};
}

} // namespace slayerlog
