#include "log_controller.hpp"

#include "clipboard.hpp"
#include "tracked_sources/log_entry_presentation.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

namespace slayerlog
{

namespace
{

bool is_before(const TextViewPosition& lhs, const TextViewPosition& rhs)
{
    return lhs.line_index < rhs.line_index || (lhs.line_index == rhs.line_index && lhs.column < rhs.column);
}

} // namespace

LogController::LogController() = default;

void LogController::reset()
{
    _buffer_a.clear();
    _buffer_b.clear();
    _active_buffer_is_a = true;
    _synced_line_count  = 0;
    _max_line_width     = 0;
    _find_state.clear();
    _time_alignment_controller.cancel();
    clear_selection();
    _text_view_controller.set_content(0, 0);
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

    _find_state.rebuild_matches(processed_sources);
    _active_buffer_is_a = !_active_buffer_is_a;
    _synced_line_count  = count;

    _max_line_width = 0;
    for (const auto& line : active_buffer())
    {
        _max_line_width = std::max(_max_line_width, static_cast<int>(line.size()));
    }

    clear_selection();
    _text_view_controller.set_content(count, _max_line_width);
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

    _find_state.expand_matches(processed_sources, AllLineIndex {_synced_line_count});
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
    const bool focused = _find_state.set_query(processed_sources, std::move(query));
    if (focused)
    {
        if (const auto visible_index = _find_state.active_visible_index(processed_sources); visible_index.has_value())
        {
            _text_view_controller.center_on_line(visible_index->value);
        }
    }
    return focused;
}

void LogController::clear_find(AllProcessedSources& processed_sources)
{
    (void)processed_sources;
    _find_state.clear();
}

bool LogController::find_active() const
{
    return _find_state.active();
}

const std::string& LogController::find_query() const
{
    return _find_state.query();
}

int LogController::total_find_match_count() const
{
    return _find_state.total_match_count();
}

int LogController::visible_find_match_count(const AllProcessedSources& processed_sources) const
{
    return _find_state.visible_match_count(processed_sources);
}

bool LogController::visible_line_matches_find(const AllProcessedSources& processed_sources, int visible_index) const
{
    return _find_state.visible_line_matches(processed_sources, visible_index);
}

bool LogController::go_to_next_find_match(const AllProcessedSources& processed_sources)
{
    if (!find_active() || total_find_match_count() == 0)
    {
        return false;
    }

    if (!_find_state.go_to_next_match(processed_sources))
    {
        return false;
    }

    const auto visible_index = _find_state.active_visible_index(processed_sources);
    if (!visible_index.has_value())
    {
        return false;
    }

    _text_view_controller.center_on_line(visible_index->value);
    return true;
}

bool LogController::go_to_previous_find_match(const AllProcessedSources& processed_sources)
{
    if (!find_active() || total_find_match_count() == 0)
    {
        return false;
    }

    if (!_find_state.go_to_previous_match(processed_sources))
    {
        return false;
    }

    const auto visible_index = _find_state.active_visible_index(processed_sources);
    if (!visible_index.has_value())
    {
        return false;
    }

    _text_view_controller.center_on_line(visible_index->value);
    return true;
}

std::optional<VisibleLineIndex> LogController::active_find_visible_index(const AllProcessedSources& processed_sources) const
{
    return _find_state.active_visible_index(processed_sources);
}

// --- Time alignment ---

bool LogController::time_alignment_active() const
{
    return _time_alignment_controller.active();
}

TimeAlignmentController& LogController::time_alignment_controller()
{
    return _time_alignment_controller;
}

const TimeAlignmentController& LogController::time_alignment_controller() const
{
    return _time_alignment_controller;
}

// --- Text selection ---

void LogController::begin_selection(TextViewPosition position)
{
    _selection_anchor      = clamp_selection_position(position);
    _selection_focus       = _selection_anchor;
    _selection_in_progress = _selection_anchor.has_value();
}

void LogController::update_selection(TextViewPosition position)
{
    if (!_selection_in_progress || !_selection_anchor.has_value())
    {
        return;
    }

    _selection_focus = clamp_selection_position(position);
}

void LogController::end_selection(std::optional<TextViewPosition> position)
{
    _selection_in_progress = false;
    if (position.has_value() && _selection_anchor.has_value())
    {
        _selection_focus = clamp_selection_position(*position);
    }
}

void LogController::clear_selection()
{
    _selection_anchor.reset();
    _selection_focus.reset();
    _selection_in_progress = false;
}

bool LogController::selection_in_progress() const
{
    return _selection_in_progress;
}

std::optional<std::pair<TextViewPosition, TextViewPosition>> LogController::selection_bounds() const
{
    if (!_selection_anchor.has_value() || !_selection_focus.has_value() || active_buffer().empty())
    {
        return std::nullopt;
    }

    auto start = clamp_selection_position(*_selection_anchor);
    auto end   = clamp_selection_position(*_selection_focus);
    if (is_before(end, start))
    {
        std::swap(start, end);
    }
    return std::pair(start, end);
}

std::string LogController::selection_text() const
{
    const auto bounds = selection_bounds();
    if (!bounds.has_value())
    {
        return {};
    }

    const auto [start, end] = *bounds;
    std::ostringstream output;
    for (int line_index = start.line_index; line_index <= end.line_index; ++line_index)
    {
        const auto& line           = line_at(line_index);
        const int line_start       = (line_index == start.line_index) ? start.column : 0;
        const int line_end         = (line_index == end.line_index) ? end.column : static_cast<int>(line.size());
        const int clamped_start    = std::clamp(line_start, 0, static_cast<int>(line.size()));
        const int clamped_end      = std::clamp(line_end, clamped_start, static_cast<int>(line.size()));
        const auto selection_count = static_cast<std::size_t>(clamped_end - clamped_start);

        output << line.substr(static_cast<std::size_t>(clamped_start), selection_count);
        if (line_index != end.line_index)
        {
            output << '\n';
        }
    }

    return output.str();
}

std::vector<TextViewRangeDecoration> LogController::selection_decorations() const
{
    std::vector<TextViewRangeDecoration> decorations;
    const auto bounds = selection_bounds();
    if (!bounds.has_value())
    {
        return decorations;
    }

    for (int line_index = bounds->first.line_index; line_index <= bounds->second.line_index; ++line_index)
    {
        const auto& line          = line_at(line_index);
        const int selection_start = (line_index == bounds->first.line_index) ? bounds->first.column : 0;
        const int selection_end   = (line_index == bounds->second.line_index) ? bounds->second.column : static_cast<int>(line.size());
        const int clamped_start   = std::clamp(selection_start, 0, static_cast<int>(line.size()));
        const int clamped_end     = std::clamp(selection_end, clamped_start, static_cast<int>(line.size()));
        if (clamped_start == clamped_end)
        {
            continue;
        }

        TextViewRangeDecoration decoration;
        decoration.line_index     = line_index;
        decoration.col_start      = clamped_start;
        decoration.col_end        = clamped_end;
        decoration.style.inverted = true;
        decorations.push_back(decoration);
    }

    return decorations;
}

bool LogController::copy_selection_to_clipboard() const
{
    return CopyTextToClipboard(selection_text());
}

// --- Event handling ---

LogEventResult LogController::handle_event(AllProcessedSources& processed_sources, ftxui::Event event, const std::function<std::optional<TextViewPosition>(int, int)>& text_position_at)
{
    // Custom event (re-render trigger)
    if (event == ftxui::Event::Custom)
    {
        return {true, false};
    }

    if (time_alignment_active())
    {
        TimeAlignmentController::FindNavigation find_navigation;
        if (find_active())
        {
            find_navigation.next                 = [this, &processed_sources]() { return go_to_next_find_match(processed_sources); };
            find_navigation.previous             = [this, &processed_sources]() { return go_to_previous_find_match(processed_sources); };
            find_navigation.active_visible_index = [this, &processed_sources]() { return active_find_visible_index(processed_sources); };
        }

        return {_time_alignment_controller.handle_event(processed_sources, _text_view_controller, event, text_position_at, find_navigation, [this]() { return copy_selection_to_clipboard(); }), false};
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

    const int fast_horizontal_step = std::max(1, (_text_view_controller.viewport_col_count() - 1) / 2);

    if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape)
    {
        return {true, true};
    }

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k'))
    {
        _text_view_controller.scroll_up(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j'))
    {
        _text_view_controller.scroll_down(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowLeft)
    {
        _text_view_controller.scroll_left(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowLeftCtrl)
    {
        _text_view_controller.scroll_left(fast_horizontal_step);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowRight)
    {
        _text_view_controller.scroll_right(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowRightCtrl)
    {
        _text_view_controller.scroll_right(fast_horizontal_step);
        return {true, false};
    }

    if (event == ftxui::Event::PageUp)
    {
        _text_view_controller.page_up();
        return {true, false};
    }

    if (event == ftxui::Event::PageDown)
    {
        _text_view_controller.page_down();
        return {true, false};
    }

    if (event == ftxui::Event::Home)
    {
        _text_view_controller.scroll_to_top();
        return {true, false};
    }

    if (event == ftxui::Event::End)
    {
        _text_view_controller.scroll_to_bottom();
        return {true, false};
    }

    if (event == ftxui::Event::C)
    {
        return {copy_selection_to_clipboard(), false};
    }

    if (event.is_mouse())
    {
        const auto mouse = event.mouse();
        if (text_position_at && mouse.button == ftxui::Mouse::Left)
        {
            const auto position = text_position_at(mouse.x, mouse.y);
            if (mouse.motion == ftxui::Mouse::Pressed)
            {
                if (position.has_value())
                {
                    begin_selection(*position);
                    return {true, false};
                }

                clear_selection();
                return {false, false};
            }

            if (mouse.motion == ftxui::Mouse::Moved && selection_in_progress())
            {
                if (position.has_value())
                {
                    update_selection(*position);
                    return {true, false};
                }
            }

            if (mouse.motion == ftxui::Mouse::Released)
            {
                end_selection(position);
                return {position.has_value(), false};
            }
        }

        if (text_position_at && mouse.button == ftxui::Mouse::Right && mouse.motion == ftxui::Mouse::Pressed)
        {
            return {copy_selection_to_clipboard(), false};
        }

        if (mouse.button == ftxui::Mouse::WheelUp)
        {
            _text_view_controller.scroll_up(1);
            return {true, false};
        }

        if (mouse.button == ftxui::Mouse::WheelDown)
        {
            _text_view_controller.scroll_down(1);
            return {true, false};
        }
    }

    return {false, false};
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

TextViewPosition LogController::clamp_selection_position(TextViewPosition position) const
{
    const auto& buffer = active_buffer();
    if (buffer.empty())
    {
        return TextViewPosition {0, 0};
    }

    position.line_index    = std::clamp(position.line_index, 0, static_cast<int>(buffer.size()) - 1);
    const auto line_length = static_cast<int>(line_at(position.line_index).size());
    position.column        = std::clamp(position.column, 0, line_length);
    return position;
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

} // namespace slayerlog
