#include "implementations/set_time_format_command.hpp"

#include <cctype>
#include <string>
#include <utility>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_controller.hpp"
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

SetTimeFormatCommand::SetTimeFormatCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& SetTimeFormatCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"set-time-format", "Set timestamp parser for one source", "set-time-format", {"Opens a source picker, then a timestamp format picker.", "Confirm each selection with Enter. All lines from that source are reparsed and all tracked lines are re-sorted."}};
    return descriptor;
}

CommandResult SetTimeFormatCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty()) { return {false, "Usage: set-time-format"}; }

    _labels = _context.tracked_sources.source_labels();
    if (_labels.empty()) { return {false, "No open sources to configure"}; }

    _formats = _context.tracked_sources.timestamp_formats();
    if (_formats.empty()) { return {false, "No timestamp formats configured"}; }

    _source_picker.emplace(_labels);
    _format_picker.reset();
    _selected_source_index = std::nullopt;
    _state = State::SourceSelection;
    return {true, "Select a source to configure", false};
}

bool SetTimeFormatCommand::has_active_interaction() const { return _state != State::Inactive; }

CommandEventResult SetTimeFormatCommand::handle_event(const ftxui::Event& event)
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
            _format_picker.emplace(_formats);
            _state = State::FormatSelection;
            return {true, CommandResult {true, "Select timestamp format for " + _labels[*selected], false}};
        }
        return {_source_picker->handle_event(event), std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        const auto selected_format = _format_picker->selected_index();
        if (!_selected_source_index.has_value() || !selected_format.has_value() || *selected_format >= _formats.size()) { return {true, CommandResult {false, "Invalid timestamp format selection", false}}; }

        const auto source_index = *_selected_source_index;
        const auto error = _context.tracked_sources.set_source_timestamp_format(source_index, _formats[*selected_format]);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("set-time-format failed source_index=" << source_index << " format_index=" << *selected_format << " error=" << *error);
            return {true, CommandResult {false, *error, false}};
        }

        reload_processed_sources(_context.tracked_sources, _context.header_text, _context.processed_sources, _context.log_controller, _context.screen);
        _state = State::Inactive;
        return {true, CommandResult {true, "Set timestamp format for " + _labels[source_index] + ": " + _formats[*selected_format]}};
    }

    return {_format_picker->handle_event(event), std::nullopt};
}

ftxui::Element SetTimeFormatCommand::render()
{
    if (_state == State::SourceSelection && _source_picker.has_value()) { return ftxui::vbox({ftxui::text("Select source to reparse") | ftxui::color(theme::muted), ftxui::separator(), _source_picker->render()}); }
    if (_state == State::FormatSelection && _format_picker.has_value()) { return ftxui::vbox({ftxui::text("Select timestamp format") | ftxui::color(theme::muted), ftxui::separator(), _format_picker->render()}); }
    return ftxui::emptyElement();
}

std::string SetTimeFormatCommand::palette_title() const { return _state == State::FormatSelection ? "Select Timestamp Format" : "Select Log Source"; }
ftxui::Element SetTimeFormatCommand::render_help() const { return picker_help("confirm"); }
void SetTimeFormatCommand::cancel() { reset(); }

void SetTimeFormatCommand::reset()
{
    _state = State::Inactive;
    _labels.clear();
    _formats.clear();
    _selected_source_index.reset();
    _source_picker.reset();
    _format_picker.reset();
}

} // namespace slayerlog
