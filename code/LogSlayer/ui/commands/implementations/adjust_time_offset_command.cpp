#include "implementations/adjust_time_offset_command.hpp"

#include <cctype>
#include <string>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_view_service.hpp"
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

AdjustTimeOffsetCommand::AdjustTimeOffsetCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& AdjustTimeOffsetCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"adjust-time-offset",
                                               "Add a timestamp offset to one source",
                                               "adjust-time-offset",
                                               {"Opens a source picker, then an offset input prompt.", "The entered offset is added on top of the source's current offset; use clear-time-offset to reset.",
                                                "Offset format is DD hh:mm:ss[.fraction], with an optional leading + or - sign.", "Example: adjust-time-offset, then enter -00 00:00:10.005"}};
    return descriptor;
}

CommandResult AdjustTimeOffsetCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: adjust-time-offset"};
    }
    _labels = _context.tracked_sources.source_display_labels();
    if (_labels.empty())
    {
        return {false, "No open sources to configure"};
    }
    _picker_labels = source_labels_with_offsets(_context.tracked_sources);
    _source_picker.emplace(_picker_labels);
    _input.reset();
    _selected_source_index = std::nullopt;
    _state                 = State::SourceSelection;
    return {true, "Select a source to offset", false};
}

bool AdjustTimeOffsetCommand::has_active_interaction() const
{
    return _state != State::Inactive;
}

CommandEventResult AdjustTimeOffsetCommand::handle_event(const ftxui::Event& event)
{
    if (_state == State::Inactive)
    {
        return {};
    }
    if (event == ftxui::Event::Escape)
    {
        cancel();
        return {true, std::nullopt};
    }

    if (_state == State::SourceSelection)
    {
        if (event == ftxui::Event::Return)
        {
            const auto selected = _source_picker->selected_index();
            if (!selected.has_value() || *selected >= _labels.size())
            {
                return {true, CommandResult {false, "Invalid source selection", false}};
            }
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
        const auto error        = _context.tracked_sources.adjust_source_timestamp_offset(source_index, *offset);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("adjust-time-offset failed source_index=" << source_index << " error=" << *error);
            return {true, CommandResult {false, *error, false}};
        }

        _context.log_view.reload(_context.tracked_sources, _context.processed_sources);
        const auto total = _context.tracked_sources.source_timestamp_offset(source_index);
        _state           = State::Inactive;
        return {true, CommandResult {true, "Adjusted time offset for " + _labels[source_index] + " by " + format_log_timestamp_offset(*offset) + " to " + format_log_timestamp_offset(total.value_or(LogTimestampOffset {}))}};
    }

    const bool handled = _input->handle_event(event);
    if (handled)
    {
        refresh_preview();
    }
    return {handled, std::nullopt};
}

ftxui::Element AdjustTimeOffsetCommand::render()
{
    if (_state == State::SourceSelection && _source_picker.has_value())
    {
        return ftxui::vbox({ftxui::text("Select source to offset") | ftxui::color(theme::muted), ftxui::separator(), _source_picker->render() | ftxui::flex});
    }
    if (_state == State::OffsetInput && _input.has_value())
    {
        const auto current      = selected_source_offset();
        const auto current_text = current.has_value() ? format_log_timestamp_offset(*current) : std::string("none");
        return ftxui::vbox({ftxui::text("Source: " + _labels[*_selected_source_index]) | ftxui::color(theme::muted), ftxui::text("Current offset: " + current_text) | ftxui::color(theme::muted),
                            ftxui::text("Expected: DD hh:mm:ss[.fraction] (added to the current offset)") | ftxui::color(theme::muted), ftxui::text("Example: -00 00:00:10.005") | ftxui::color(theme::muted), ftxui::separator(),
                            _input->render()});
    }
    return ftxui::emptyElement();
}

std::string AdjustTimeOffsetCommand::palette_title() const
{
    return _state == State::OffsetInput ? "Adjust Timestamp Offset" : "Select Log Source";
}

ftxui::Element AdjustTimeOffsetCommand::render_help() const
{
    if (_state == State::OffsetInput)
    {
        auto sep = []() { return ftxui::text("  "); };
        return ftxui::hbox({theme::key_hint("Enter", "apply"), sep(), theme::key_hint("Esc", "cancel")});
    }
    return picker_help("confirm");
}

void AdjustTimeOffsetCommand::cancel()
{
    reset();
}

void AdjustTimeOffsetCommand::reset()
{
    _state = State::Inactive;
    _labels.clear();
    _picker_labels.clear();
    _selected_source_index.reset();
    _source_picker.reset();
    _input.reset();
}

std::optional<LogTimestampOffset> AdjustTimeOffsetCommand::selected_source_offset() const
{
    if (!_selected_source_index.has_value())
    {
        return std::nullopt;
    }
    return _context.tracked_sources.source_timestamp_offset(*_selected_source_index);
}

void AdjustTimeOffsetCommand::refresh_preview()
{
    if (!_input.has_value())
    {
        return;
    }
    if (_input->text().empty())
    {
        _input->set_preview("Enter offset as DD hh:mm:ss[.fraction]", false);
        return;
    }
    const auto offset = parse_log_timestamp_offset(_input->text());
    if (!offset.has_value())
    {
        _input->set_preview("Invalid offset: expected DD hh:mm:ss[.fraction]", true);
        return;
    }

    const auto total = add_offsets(selected_source_offset().value_or(LogTimestampOffset {}), *offset);
    if (!total.has_value())
    {
        _input->set_preview("Offset would overflow", true);
        return;
    }
    _input->set_preview("Adds " + format_log_timestamp_offset(*offset) + " -> total " + format_log_timestamp_offset(*total), false);
}

} // namespace slayerlog
