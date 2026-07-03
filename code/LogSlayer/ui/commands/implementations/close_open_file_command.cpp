#include "implementations/close_open_file_command.hpp"

#include <cctype>
#include <string>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "view_theme.hpp"

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

CloseOpenFileCommand::CloseOpenFileCommand(CommandContext context) : _context(context)
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

    _labels = _context.tracked_sources.source_display_labels();
    if (_labels.empty())
    {
        return {false, "No open files to close"};
    }

    _picker.emplace(_labels);
    _active = true;
    return {true, "Select a file to close", false};
}

bool CloseOpenFileCommand::has_active_interaction() const
{
    return _active;
}

CommandEventResult CloseOpenFileCommand::handle_event(const ftxui::Event& event)
{
    if (!_active || !_picker.has_value())
    {
        return {};
    }

    if (event == ftxui::Event::Escape)
    {
        cancel();
        return {true, std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        const auto selected = _picker->selected_index();
        if (!selected.has_value())
        {
            return {true, CommandResult {false, "No open file is selected.", false}};
        }

        std::string closed_label;
        const auto error = _context.tracked_sources.close_source(*selected, &closed_label);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("close-open-file failed selected_index=" << *selected << " error=" << *error);
            return {true, CommandResult {false, *error, false}};
        }

        _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
        _active = false;
        return {true, CommandResult {true, "Closed file: " + closed_label}};
    }

    return {_picker->handle_event(event), std::nullopt};
}

ftxui::Element CloseOpenFileCommand::render()
{
    if (!_picker.has_value())
    {
        return ftxui::emptyElement();
    }
    return ftxui::vbox({ftxui::text("Select file to close") | ftxui::color(theme::muted), ftxui::separator(), _picker->render() | ftxui::flex});
}

std::string CloseOpenFileCommand::palette_title() const
{
    return "Close Open File";
}

ftxui::Element CloseOpenFileCommand::render_help() const
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({theme::key_hint("Up/Down", "select"), sep(), theme::key_hint("Enter", "close file"), sep(), theme::key_hint("Esc", "cancel")});
}

void CloseOpenFileCommand::cancel()
{
    _active = false;
    _labels.clear();
    _picker.reset();
}

} // namespace slayerlog
