#include "implementations/align_time_view_command.hpp"

#include <cctype>
#include <string>

#include "command_support.hpp"
#include "log_view_service.hpp"
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

} // namespace

AlignTimeViewCommand::AlignTimeViewCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& AlignTimeViewCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"align-time",
                                               "Align one source's timestamps against the others",
                                               "align-time",
                                               {"Opens a source picker, then a side-by-side view: the other sources on the left, the chosen source on the right.",
                                                "Pick the line to align (right), mark one or two reference lines (left), then nudge the line into place with the arrow keys.",
                                                "Enter applies the offset (added on top of any existing offset); Esc cancels. Use clear-time-offset to reset a source."}};
    return descriptor;
}

CommandResult AlignTimeViewCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: align-time"};
    }

    _labels = _context.tracked_sources.source_display_labels();
    if (_labels.size() < 2)
    {
        return {false, "Need at least two open sources to align"};
    }

    _picker_labels = source_labels_with_offsets(_context.tracked_sources);
    _source_picker.emplace(_picker_labels);
    _active = true;
    return {true, "Select a source to align", false};
}

bool AlignTimeViewCommand::has_active_interaction() const
{
    return _active;
}

CommandEventResult AlignTimeViewCommand::handle_event(const ftxui::Event& event)
{
    if (!_active)
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
        const auto selected = _source_picker->selected_index();
        if (!selected.has_value() || *selected >= _labels.size())
        {
            return {true, CommandResult {false, "Invalid source selection", false}};
        }

        const std::string label        = _labels[*selected];
        const std::size_t source_index = *selected;
        reset();
        _context.log_view.begin_time_alignment(source_index);
        return {true, CommandResult {true, "Aligning " + label}};
    }

    return {_source_picker->handle_event(event), std::nullopt};
}

ftxui::Element AlignTimeViewCommand::render()
{
    if (_active && _source_picker.has_value())
    {
        return ftxui::vbox({ftxui::text("Select source to align") | ftxui::color(theme::muted), ftxui::separator(), _source_picker->render() | ftxui::flex});
    }

    return ftxui::emptyElement();
}

std::string AlignTimeViewCommand::palette_title() const
{
    return "Align Time — Select Source";
}

ftxui::Element AlignTimeViewCommand::render_help() const
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({theme::key_hint("Up/Down", "select"), sep(), theme::key_hint("Enter", "align"), sep(), theme::key_hint("Esc", "cancel")});
}

void AlignTimeViewCommand::cancel()
{
    reset();
}

void AlignTimeViewCommand::reset()
{
    _active = false;
    _labels.clear();
    _picker_labels.clear();
    _source_picker.reset();
}

} // namespace slayerlog
