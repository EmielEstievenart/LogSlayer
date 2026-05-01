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

// --- Event handling ---

LogEventResult LogController::handle_event(AllProcessedSources& processed_sources, ftxui::Event event, const std::function<std::optional<TextViewPosition>(const ftxui::Mouse&)>& mouse_to_text_position)
{
    // Escape: clear find if active, otherwise delegate (TextViewController handles exit)
    if (event == ftxui::Event::Escape && find_active())
    {
        clear_find(processed_sources);
        return {true, false};
    }

    // Custom event (re-render trigger)
    if (event == ftxui::Event::Custom)
    {
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

} // namespace slayerlog
