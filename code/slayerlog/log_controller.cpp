#include "log_controller.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

namespace slayerlog
{

LogController::LogController() = default;

void LogController::reset()
{
    _buffer_a.clear();
    _buffer_b.clear();
    _active_buffer_is_a = true;
    _synced_line_count  = 0;
    _max_line_width     = 0;
    _find_query.clear();
    _find_pattern.reset();
    _find_match_entry_indices.clear();
    _active_find_entry_index.reset();
    reset_time_alignment();
    _text_view_controller.set_content(0, 0, [this](int index) -> const std::string& { return active_buffer()[static_cast<std::size_t>(index)]; });
    _text_view_controller.scroll_to_bottom();
}

// --- Content management ---

void LogController::rebuild_view(const AllProcessedSources& processed_sources)
{
    auto& target = inactive_buffer();
    target.clear();

    const int count = processed_sources.line_count();
    target.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        target.push_back(processed_sources.rendered_line(i));
    }

    rebuild_find_matches(processed_sources);
    _active_buffer_is_a = !_active_buffer_is_a;
    _synced_line_count  = count;

    _max_line_width = 0;
    for (const auto& line : active_buffer())
    {
        _max_line_width = std::max(_max_line_width, static_cast<int>(line.size()));
    }

    _text_view_controller.set_content(count, _max_line_width, [this](int index) -> const std::string& { return active_buffer()[static_cast<std::size_t>(index)]; });
}

void LogController::sync_view(const AllProcessedSources& processed_sources)
{
    const int current_count = processed_sources.line_count();
    if (current_count < _synced_line_count)
    {
        // Line count decreased (shouldn't happen for streaming, but handle safely)
        rebuild_view(processed_sources);
        return;
    }

    if (_synced_line_count > 0 && static_cast<int>(active_buffer().size()) >= _synced_line_count && active_buffer().back() != processed_sources.rendered_line(_synced_line_count - 1))
    {
        rebuild_view(processed_sources);
        return;
    }

    if (current_count == _synced_line_count)
    {
        return;
    }

    auto& buffer = active_buffer();
    buffer.reserve(static_cast<std::size_t>(current_count));
    for (int i = _synced_line_count; i < current_count; ++i)
    {
        buffer.push_back(processed_sources.rendered_line(i));
        _max_line_width = std::max(_max_line_width, static_cast<int>(buffer.back().size()));
    }

    expand_find_matches(processed_sources, AllLineIndex {_synced_line_count});
    _synced_line_count = current_count;
    _text_view_controller.update_content_size(current_count, _max_line_width);
}

// --- Domain-specific navigation ---

bool LogController::go_to_line(const AllProcessedSources& processed_sources, int line_number)
{
    const auto target_visible_index = processed_sources.visible_line_index_for_line_number(line_number);
    if (!target_visible_index.has_value())
    {
        return false;
    }

    _text_view_controller.center_on_line(target_visible_index->value);
    return true;
}

// --- Find ---

bool LogController::set_find_query(AllProcessedSources& processed_sources, std::string query)
{
    query = trim_search_text(query);
    if (query.empty())
    {
        clear_find(processed_sources);
        return false;
    }

    const SearchPattern pattern = compile_search_pattern(query);

    _find_query   = pattern.raw_text;
    _find_pattern = pattern;
    rebuild_find_matches(processed_sources);
    _active_find_entry_index.reset();
    const bool has_matches = !_find_match_entry_indices.empty();
    if (!has_matches)
    {
        return false;
    }

    return go_to_next_find_match(processed_sources);
}

void LogController::clear_find(AllProcessedSources& processed_sources)
{
    (void)processed_sources;
    _find_query.clear();
    _find_pattern.reset();
    _find_match_entry_indices.clear();
    _active_find_entry_index.reset();
}

bool LogController::find_active() const
{
    return !_find_query.empty();
}

const std::string& LogController::find_query() const
{
    return _find_query;
}

int LogController::total_find_match_count() const
{
    return static_cast<int>(_find_match_entry_indices.size());
}

int LogController::visible_find_match_count(const AllProcessedSources& processed_sources) const
{
    return static_cast<int>(std::count_if(_find_match_entry_indices.begin(), _find_match_entry_indices.end(), [&](AllLineIndex entry_index) { return processed_sources.entry_index_is_visible(entry_index); }));
}

bool LogController::visible_line_matches_find(const AllProcessedSources& processed_sources, int visible_index) const
{
    if (visible_index < 0)
    {
        return false;
    }

    const auto entry_index = processed_sources.entry_index_for_visible_line(VisibleLineIndex {visible_index});
    if (!entry_index.has_value())
    {
        return false;
    }

    return std::binary_search(_find_match_entry_indices.begin(), _find_match_entry_indices.end(), *entry_index);
}

bool LogController::go_to_next_find_match(const AllProcessedSources& processed_sources)
{
    if (!find_active() || total_find_match_count() == 0)
    {
        return false;
    }

    int current_position = -1;
    if (_active_find_entry_index.has_value())
    {
        const auto position = std::find(_find_match_entry_indices.begin(), _find_match_entry_indices.end(), *_active_find_entry_index);
        if (position != _find_match_entry_indices.end())
        {
            current_position = static_cast<int>(std::distance(_find_match_entry_indices.begin(), position));
        }
    }

    for (int offset = 1; offset <= total_find_match_count(); ++offset)
    {
        const int next_position        = (current_position + offset) % total_find_match_count();
        const AllLineIndex entry_index = _find_match_entry_indices[FindResultIndex {next_position}];
        if (!processed_sources.entry_index_is_visible(entry_index))
        {
            continue;
        }

        _active_find_entry_index = entry_index;
        const auto visible_index = processed_sources.visible_line_index_for_entry(entry_index);
        if (!visible_index.has_value())
        {
            return false;
        }

        _text_view_controller.center_on_line(visible_index->value);
        return true;
    }

    return false;
}

bool LogController::go_to_previous_find_match(const AllProcessedSources& processed_sources)
{
    if (!find_active() || total_find_match_count() == 0)
    {
        return false;
    }

    int current_position = 0;
    if (_active_find_entry_index.has_value())
    {
        const auto position = std::find(_find_match_entry_indices.begin(), _find_match_entry_indices.end(), *_active_find_entry_index);
        if (position != _find_match_entry_indices.end())
        {
            current_position = static_cast<int>(std::distance(_find_match_entry_indices.begin(), position));
        }
    }

    for (int offset = 1; offset <= total_find_match_count(); ++offset)
    {
        const int previous_position    = (current_position - offset + total_find_match_count()) % total_find_match_count();
        const AllLineIndex entry_index = _find_match_entry_indices[FindResultIndex {previous_position}];
        if (!processed_sources.entry_index_is_visible(entry_index))
        {
            continue;
        }

        _active_find_entry_index = entry_index;
        const auto visible_index = processed_sources.visible_line_index_for_entry(entry_index);
        if (!visible_index.has_value())
        {
            return false;
        }

        _text_view_controller.center_on_line(visible_index->value);
        return true;
    }

    return false;
}

std::optional<VisibleLineIndex> LogController::active_find_visible_index(const AllProcessedSources& processed_sources) const
{
    if (!_active_find_entry_index.has_value())
    {
        return std::nullopt;
    }

    return processed_sources.visible_line_index_for_entry(*_active_find_entry_index);
}

// --- Time alignment ---

void LogController::start_time_alignment(TimeAlignmentApplyCallback apply_callback)
{
    _time_alignment_apply_callback = std::move(apply_callback);
    _time_alignment_phase          = TimeAlignmentPhase::SelectSource;
    _time_alignment_source.reset();
    _time_alignment_selected_line = std::max(0, _text_view_controller.first_visible_line());
    set_time_alignment_status("Select source line");
}

void LogController::cancel_time_alignment()
{
    reset_time_alignment();
}

bool LogController::time_alignment_active() const
{
    return _time_alignment_phase != TimeAlignmentPhase::Inactive;
}

std::optional<int> LogController::time_alignment_selected_line() const
{
    return _time_alignment_selected_line;
}

std::string LogController::time_alignment_status_text() const
{
    return _time_alignment_status;
}

bool LogController::time_alignment_status_is_error() const
{
    return _time_alignment_status_is_error;
}

// --- Event handling ---

LogEventResult LogController::handle_event(AllProcessedSources& processed_sources, ftxui::Event event, const std::function<std::optional<TextViewPosition>(const ftxui::Mouse&)>& mouse_to_text_position)
{
    // Custom event (re-render trigger)
    if (event == ftxui::Event::Custom)
    {
        return {true, false};
    }

    if (time_alignment_active())
    {
        return {handle_time_alignment_event(processed_sources, event, mouse_to_text_position), false};
    }

    // Escape: clear find if active, otherwise delegate (TextViewController handles exit)
    if (event == ftxui::Event::Escape && find_active())
    {
        clear_find(processed_sources);
        return {true, false};
    }

    // Pause toggle
    if (event == ftxui::Event::Character('p'))
    {
        processed_sources.toggle_pause();
        if (!processed_sources.updates_paused())
        {
            // Unpausing flushes buffered updates
            rebuild_view(processed_sources);
            (void)processed_sources.consume_column_width_growth();
        }
        return {true, false};
    }

    // Find navigation: intercept arrow keys when find is active
    if (find_active() && event == ftxui::Event::ArrowRight)
    {
        return {go_to_next_find_match(processed_sources), false};
    }

    if (find_active() && event == ftxui::Event::ArrowLeft)
    {
        return {go_to_previous_find_match(processed_sources), false};
    }

    // Delegate everything else to the generic text view controller
    auto result = _text_view_controller.parse_event(event, mouse_to_text_position);
    return {result.handled, result.request_exit};
}

// --- Access to underlying text view ---

TextViewController& LogController::text_view_controller()
{
    return _text_view_controller;
}

const TextViewController& LogController::text_view_controller() const
{
    return _text_view_controller;
}

const std::string& LogController::line_at(int index) const
{
    return active_buffer().at(static_cast<std::size_t>(index));
}

// --- Private ---

std::vector<std::string>& LogController::active_buffer()
{
    return _active_buffer_is_a ? _buffer_a : _buffer_b;
}

const std::vector<std::string>& LogController::active_buffer() const
{
    return _active_buffer_is_a ? _buffer_a : _buffer_b;
}

std::vector<std::string>& LogController::inactive_buffer()
{
    return _active_buffer_is_a ? _buffer_b : _buffer_a;
}

void LogController::rebuild_find_matches(const AllProcessedSources& processed_sources)
{
    _find_match_entry_indices.clear();
    if (!find_active())
    {
        return;
    }

    _find_match_entry_indices.reserve(static_cast<std::size_t>(processed_sources.total_line_count()));
    for (int index = 0; index < processed_sources.total_line_count(); ++index)
    {
        const AllLineIndex entry_index {index};
        if (entry_matches_find_query(processed_sources.entry_at(entry_index)))
        {
            _find_match_entry_indices.push_back(entry_index);
        }
    }
}

void LogController::expand_find_matches(const AllProcessedSources& processed_sources, AllLineIndex first_new_entry_index)
{
    if (!find_active())
    {
        return;
    }

    for (int index = first_new_entry_index.value; index < processed_sources.total_line_count(); ++index)
    {
        const AllLineIndex entry_index {index};
        if (entry_matches_find_query(processed_sources.entry_at(entry_index)))
        {
            _find_match_entry_indices.push_back(entry_index);
        }
    }
}

bool LogController::entry_matches_find_query(const LogEntry& entry) const
{
    return _find_pattern.has_value() && matches_pattern(entry.text, *_find_pattern);
}

bool LogController::handle_time_alignment_event(AllProcessedSources& processed_sources, ftxui::Event event, const std::function<std::optional<TextViewPosition>(const ftxui::Mouse&)>& mouse_to_text_position)
{
    if (event == ftxui::Event::Escape)
    {
        cancel_time_alignment();
        return true;
    }

    if (event == ftxui::Event::Return)
    {
        return confirm_time_alignment_selection(processed_sources);
    }

    if (find_active() && event == ftxui::Event::ArrowRight)
    {
        const bool moved = go_to_next_find_match(processed_sources);
        const auto active_visible_index = active_find_visible_index(processed_sources);
        if (active_visible_index.has_value())
        {
            set_time_alignment_selected_line(processed_sources, active_visible_index->value, true);
        }
        return moved;
    }

    if (find_active() && event == ftxui::Event::ArrowLeft)
    {
        const bool moved = go_to_previous_find_match(processed_sources);
        const auto active_visible_index = active_find_visible_index(processed_sources);
        if (active_visible_index.has_value())
        {
            set_time_alignment_selected_line(processed_sources, active_visible_index->value, true);
        }
        return moved;
    }

    const int page_step = std::max(1, _text_view_controller.viewport_line_count() - 1);
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k'))
    {
        move_time_alignment_selection(processed_sources, -1);
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j'))
    {
        move_time_alignment_selection(processed_sources, 1);
        return true;
    }

    if (event == ftxui::Event::PageUp)
    {
        move_time_alignment_selection(processed_sources, -page_step);
        return true;
    }

    if (event == ftxui::Event::PageDown)
    {
        move_time_alignment_selection(processed_sources, page_step);
        return true;
    }

    if (event == ftxui::Event::Home)
    {
        set_time_alignment_selected_line(processed_sources, 0, true);
        return true;
    }

    if (event == ftxui::Event::End)
    {
        set_time_alignment_selected_line(processed_sources, processed_sources.line_count() - 1, true);
        return true;
    }

    if (event.is_mouse())
    {
        if (event.mouse().button == ftxui::Mouse::WheelUp)
        {
            move_time_alignment_selection(processed_sources, -1);
            return true;
        }

        if (event.mouse().button == ftxui::Mouse::WheelDown)
        {
            move_time_alignment_selection(processed_sources, 1);
            return true;
        }

        if (mouse_to_text_position && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed)
        {
            const auto position = mouse_to_text_position(event.mouse());
            if (position.has_value())
            {
                set_time_alignment_selected_line(processed_sources, position->line_index, true);
                return true;
            }
        }
    }

    const auto result = _text_view_controller.parse_event(event, mouse_to_text_position);
    return result.handled;
}

void LogController::reset_time_alignment()
{
    _time_alignment_phase = TimeAlignmentPhase::Inactive;
    _time_alignment_selected_line.reset();
    _time_alignment_source.reset();
    _time_alignment_status.clear();
    _time_alignment_status_is_error = false;
    _time_alignment_apply_callback  = {};
}

void LogController::set_time_alignment_status(std::string message, bool is_error)
{
    _time_alignment_status          = std::move(message);
    _time_alignment_status_is_error = is_error;
}

void LogController::set_time_alignment_selected_line(const AllProcessedSources& processed_sources, int visible_line_index, bool keep_visible)
{
    if (processed_sources.line_count() <= 0)
    {
        _time_alignment_selected_line.reset();
        return;
    }

    _time_alignment_selected_line = std::clamp(visible_line_index, 0, processed_sources.line_count() - 1);
    if (keep_visible)
    {
        _text_view_controller.center_on_line(*_time_alignment_selected_line);
    }
}

void LogController::move_time_alignment_selection(const AllProcessedSources& processed_sources, int delta)
{
    const int current_line = _time_alignment_selected_line.value_or(_text_view_controller.first_visible_line());
    set_time_alignment_selected_line(processed_sources, current_line + delta, true);
}

bool LogController::confirm_time_alignment_selection(AllProcessedSources& processed_sources)
{
    const LogEntry* selected_entry = time_alignment_selected_entry(processed_sources);
    if (selected_entry == nullptr)
    {
        set_time_alignment_status("Select a visible log entry, not a collapsed summary row", true);
        return true;
    }

    if (_time_alignment_phase == TimeAlignmentPhase::SelectSource)
    {
        if (!selected_entry->metadata.timestamp.has_value())
        {
            set_time_alignment_status("Source line has no original timestamp", true);
            return true;
        }

        _time_alignment_source = TimeAlignmentSource {
            selected_entry->metadata.source_index,
            selected_entry->metadata.source_label,
            *selected_entry->metadata.timestamp,
        };
        _time_alignment_phase = TimeAlignmentPhase::SelectDestination;
        set_time_alignment_status("Source " + selected_entry->metadata.source_label + " selected, select destination line");
        return true;
    }

    if (!_time_alignment_source.has_value())
    {
        set_time_alignment_status("No source line selected", true);
        _time_alignment_phase = TimeAlignmentPhase::SelectSource;
        return true;
    }

    const auto destination_timestamp = effective_timestamp(selected_entry->metadata);
    if (!destination_timestamp.has_value())
    {
        set_time_alignment_status("Destination line has no timestamp", true);
        return true;
    }

    if (selected_entry->metadata.source_index == _time_alignment_source->source_index)
    {
        set_time_alignment_status("Destination must be from a different source", true);
        return true;
    }

    if (!_time_alignment_apply_callback)
    {
        set_time_alignment_status("Time alignment handler is unavailable", true);
        return true;
    }

    LogEntry source_entry;
    source_entry.metadata.timestamp    = _time_alignment_source->timestamp;
    source_entry.metadata.source_index = _time_alignment_source->source_index;
    source_entry.metadata.source_label = _time_alignment_source->source_label;

    const TimeAlignmentApplyResult result = _time_alignment_apply_callback(source_entry, *selected_entry);
    if (!result.success)
    {
        set_time_alignment_status(result.message, true);
        return true;
    }

    reset_time_alignment();
    return true;
}

const LogEntry* LogController::time_alignment_selected_entry(const AllProcessedSources& processed_sources) const
{
    if (!_time_alignment_selected_line.has_value())
    {
        return nullptr;
    }

    const auto entry_index = processed_sources.entry_index_for_visible_line(VisibleLineIndex {*_time_alignment_selected_line});
    if (!entry_index.has_value())
    {
        return nullptr;
    }

    return &processed_sources.entry_at(*entry_index);
}

} // namespace slayerlog
