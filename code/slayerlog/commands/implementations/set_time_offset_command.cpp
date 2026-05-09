#include "implementations/set_time_offset_command.hpp"

#include <cctype>
#include <string>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_controller.hpp"
#include "timestamp/log_timestamp.hpp"
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
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) { ++start; }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) { --end; }
    return std::string(text.substr(start, end - start));
}

ftxui::Element picker_help(std::string enter_label)
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({theme::key_hint("Up/Down", "select"), sep(), theme::key_hint("Enter", enter_label), sep(), theme::key_hint("Esc", "cancel")});
}
}

SetTimeOffsetCommand::SetTimeOffsetCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& SetTimeOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"set-time-offset", "Set timestamp offset for one source", "set-time-offset", {"Opens a source picker, then an offset input prompt.", "Offset format is DD hh:mm:ss[.fraction], with an optional leading + or - sign.", "Example: set-time-offset, then enter 20 02:10:10.005"}};
    return descriptor;
}

CommandResult SetTimeOffsetCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: set-time-offset"}; }
    _labels = _context.tracked_sources.source_labels();
    if (_labels.empty()) { return {false, "No open sources to configure"}; }
    _source_picker.emplace(_labels);
    _input.reset();
    _selected_source_index = std::nullopt;
    _state = State::SourceSelection;
    return {true, "Select a source to offset", false};
}

bool SetTimeOffsetCommand::has_active_interaction() const { return _state != State::Inactive; }

CommandEventResult SetTimeOffsetCommand::handle_event(const ftxui::Event& event)
{
    if (_state == State::Inactive) { return {}; }
    if (event == ftxui::Event::Escape) { cancel(); return {true, std::nullopt}; }

    if (_state == State::SourceSelection)
    {
        if (event == ftxui::Event::Return)
        {
            const auto selected = _source_picker->selected_index();
            if (!selected.has_value() || *selected >= _labels.size()) { return {true, CommandResult {false, "Invalid source selection", false}}; }
            _selected_source_index = *selected;
            _input.emplace();
            refresh_preview();
            _state = State::OffsetInput;
            return {true, CommandResult {true, "Enter timestamp offset for " + _labels[*selected], false}};
        }
        return {_source_picker->handle_event(event), std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        const auto offset = parse_log_timestamp_offset(_input->text());
        if (!offset.has_value())
        {
            refresh_preview();
            return {true, CommandResult {false, "Invalid offset: expected DD hh:mm:ss[.fraction]", false}};
        }

        const auto source_index = *_selected_source_index;
        const auto error = _context.tracked_sources.set_source_timestamp_offset(source_index, *offset);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("set-time-offset failed source_index=" << source_index << " error=" << *error);
            return {true, CommandResult {false, *error, false}};
        }

        reload_processed_sources(_context.tracked_sources, _context.header_text, _context.processed_sources, _context.log_controller, _context.screen);
        _state = State::Inactive;
        return {true, CommandResult {true, "Set time offset for " + _labels[source_index] + ": " + format_log_timestamp_offset(*offset)}};
    }

    const bool handled = _input->handle_event(event);
    if (handled) { refresh_preview(); }
    return {handled, std::nullopt};
}

ftxui::Element SetTimeOffsetCommand::render()
{
    if (_state == State::SourceSelection && _source_picker.has_value()) { return ftxui::vbox({ftxui::text("Select source to offset") | ftxui::color(theme::muted), ftxui::separator(), _source_picker->render() | ftxui::flex}); }
    if (_state == State::OffsetInput && _input.has_value()) { return ftxui::vbox({ftxui::text("Source: " + _labels[*_selected_source_index]) | ftxui::color(theme::muted), ftxui::text("Expected: DD hh:mm:ss[.fraction]") | ftxui::color(theme::muted), ftxui::text("Example: 20 02:10:10.005") | ftxui::color(theme::muted), ftxui::separator(), _input->render()}); }
    return ftxui::emptyElement();
}

std::string SetTimeOffsetCommand::palette_title() const { return _state == State::OffsetInput ? "Set Timestamp Offset" : "Select Log Source"; }

ftxui::Element SetTimeOffsetCommand::render_help() const
{
    if (_state == State::OffsetInput)
    {
        auto sep = []() { return ftxui::text("  "); };
        return ftxui::hbox({theme::key_hint("Enter", "apply"), sep(), theme::key_hint("Esc", "cancel")});
    }
    return picker_help("confirm");
}

void SetTimeOffsetCommand::cancel() { reset(); }

void SetTimeOffsetCommand::reset()
{
    _state = State::Inactive;
    _labels.clear();
    _selected_source_index.reset();
    _source_picker.reset();
    _input.reset();
}

void SetTimeOffsetCommand::refresh_preview()
{
    if (!_input.has_value()) { return; }
    if (_input->text().empty()) { _input->set_preview("Enter offset as DD hh:mm:ss[.fraction]", false); return; }
    const auto offset = parse_log_timestamp_offset(_input->text());
    if (!offset.has_value()) { _input->set_preview("Invalid offset: expected DD hh:mm:ss[.fraction]", true); return; }
    _input->set_preview("Applies offset: " + format_log_timestamp_offset(*offset), false);
}

} // namespace slayerlog
