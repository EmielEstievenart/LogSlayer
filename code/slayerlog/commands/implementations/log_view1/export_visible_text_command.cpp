#include "implementations/export_visible_text_command.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

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
}

ExportVisibleTextCommand::ExportVisibleTextCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& ExportVisibleTextCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"export-visible-text", "Write visible rendered lines to a file", "export-visible-text <path>", {"Exports the currently visible rendered lines after filters and hidden columns are applied.", "This writes all visible lines in the model, not just the current viewport.", "Example: export-visible-text filtered.log"}};
    return descriptor;
}

CommandResult ExportVisibleTextCommand::execute(std::string_view arguments)
{
    const std::string trimmed_path = trim_text(arguments);
    if (trimmed_path.empty()) { return {false, "Usage: export-visible-text <path>"}; }

    const std::filesystem::path output_path(trimmed_path);
    std::error_code error_code;
    if (output_path.has_parent_path() && !std::filesystem::exists(output_path.parent_path(), error_code))
    {
        if (error_code) { return {false, "Could not access parent folder: " + output_path.parent_path().string()}; }
        return {false, "Parent folder does not exist: " + output_path.parent_path().string()};
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) { return {false, "Could not open file for writing: " + trimmed_path}; }

    for (int line_index = 0; line_index < _context.processed_sources.line_count(); ++line_index)
    {
        if (line_index > 0) { output << '\n'; }
        output << _context.processed_sources.rendered_line(line_index);
        if (!output) { return {false, "Failed while writing file: " + trimmed_path}; }
    }

    output.close();
    if (!output) { return {false, "Failed while writing file: " + trimmed_path}; }
    return {true, "Exported " + std::to_string(_context.processed_sources.line_count()) + " visible lines to " + trimmed_path};
}

} // namespace slayerlog
