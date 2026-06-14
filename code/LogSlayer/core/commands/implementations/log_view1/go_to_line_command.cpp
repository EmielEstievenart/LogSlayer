#include "implementations/log_view1/go_to_line_command.hpp"

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

GoToLineCommand::GoToLineCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& GoToLineCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"go-to-line", "Center the view on a line number", "go-to-line <line-number>", {"Jumps to a raw line number after all currently loaded sources are merged.", "The target line must still be visible after filters and cutoffs are applied."}};
    return descriptor;
}

CommandResult GoToLineCommand::execute(std::string_view arguments)
{
    const auto line_number = parse_positive_line_number(arguments);
    if (!line_number.has_value()) { return {false, "Usage: go-to-line <line-number>"}; }
    if (*line_number > _context.processed_sources.total_line_count()) { return {false, "Line " + std::to_string(*line_number) + " is out of range"}; }
    if (!_context.log_view.go_to_line(_context.processed_sources, *line_number)) { return {false, "Line " + std::to_string(*line_number) + " is hidden by current filters or line cutoff"}; }
    return {true, "Centered view on line " + std::to_string(*line_number)};
}

} // namespace slayerlog
