#include "implementations/clear_time_offset_command.hpp"

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

ftxui::Element picker_help(std::string enter_label)
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({theme::key_hint("Up/Down", "select"), sep(), theme::key_hint("Enter", enter_label), sep(), theme::key_hint("Esc", "cancel")});
}
}

ClearTimeOffsetCommand::ClearTimeOffsetCommand(CommandContext context) : _context(context)
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
    _labels = _context.tracked_sources.source_display_labels();
    if (_labels.empty())
    {
        return {false, "No open sources to configure"};
    }
    _picker.emplace(_labels);
    _active = true;
    return {true, "Select a source to clear offset", false};
}

bool ClearTimeOffsetCommand::has_active_interaction() const
{
    return _active;
}

CommandEventResult ClearTimeOffsetCommand::handle_event(const ftxui::Event& event)
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
        if (!selected.has_value() || *selected >= _labels.size())
        {
            return {true, CommandResult {false, "Invalid source selection", false}};
        }
        const auto error = _context.tracked_sources.clear_source_timestamp_offset(*selected);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("clear-time-offset failed source_index=" << *selected << " error=" << *error);
            return {true, CommandResult {false, *error, false}};
        }
        _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
        _active = false;
        return {true, CommandResult {true, "Cleared time offset for " + _labels[*selected]}};
    }
    return {_picker->handle_event(event), std::nullopt};
}

ftxui::Element ClearTimeOffsetCommand::render()
{
    if (!_picker.has_value())
    {
        return ftxui::emptyElement();
    }
    return ftxui::vbox({ftxui::text("Select source to clear offset") | ftxui::color(theme::muted), ftxui::separator(), _picker->render() | ftxui::flex});
}

std::string ClearTimeOffsetCommand::palette_title() const
{
    return "Select Log Source";
}
ftxui::Element ClearTimeOffsetCommand::render_help() const
{
    return picker_help("confirm");
}

void ClearTimeOffsetCommand::cancel()
{
    _active = false;
    _labels.clear();
    _picker.reset();
}

} // namespace slayerlog
