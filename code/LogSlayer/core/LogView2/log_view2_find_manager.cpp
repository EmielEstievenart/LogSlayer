#include "log_view2_find_manager.hpp"

#include <algorithm>
#include <utility>

namespace slayerlog
{

LogView2FindManager::LogView2FindManager(std::shared_ptr<LogView2Data> data) : _data(std::move(data))
{
    if (_data != nullptr)
    {
        _update_callback_id = _data->add_update_callback([this](VisibleLineIndex first_changed_line) { on_data_changed(first_changed_line); });
    }
}

LogView2FindManager::~LogView2FindManager()
{
    if (_data != nullptr)
    {
        _data->remove_update_callback(_update_callback_id);
    }
}

bool LogView2FindManager::set_query(std::string query)
{
    query = trim_search_text(query);
    if (query.empty())
    {
        clear();
        return false;
    }

    const SearchPattern pattern = compile_search_pattern(query);
    _query                      = pattern.raw_text;
    _pattern                    = pattern;
    _active_match_line.reset();
    _pending_focus_line.reset();
    rebuild_matches();
    return go_to_next_match();
}

void LogView2FindManager::clear()
{
    _query.clear();
    _pattern.reset();
    _matching_lines.clear();
    _active_match_line.reset();
    _pending_focus_line.reset();
}

bool LogView2FindManager::active() const
{
    return !_query.empty();
}

const std::string& LogView2FindManager::query() const
{
    return _query;
}

std::size_t LogView2FindManager::match_count() const
{
    return _matching_lines.size();
}

bool LogView2FindManager::line_matches(std::size_t line_index) const
{
    return std::binary_search(_matching_lines.begin(), _matching_lines.end(), line_index);
}

bool LogView2FindManager::line_is_active_match(std::size_t line_index) const
{
    return _active_match_line.has_value() && *_active_match_line == line_index;
}

std::optional<std::size_t> LogView2FindManager::active_match_line() const
{
    return _active_match_line;
}

bool LogView2FindManager::go_to_next_match()
{
    if (!active() || _matching_lines.empty())
    {
        return false;
    }

    if (!_active_match_line.has_value())
    {
        return focus_match_at(0);
    }

    const auto current = std::find(_matching_lines.begin(), _matching_lines.end(), *_active_match_line);
    if (current == _matching_lines.end())
    {
        return focus_match_at(0);
    }

    const std::size_t next_position = (static_cast<std::size_t>(std::distance(_matching_lines.begin(), current)) + 1) % _matching_lines.size();
    return focus_match_at(next_position);
}

bool LogView2FindManager::go_to_previous_match()
{
    if (!active() || _matching_lines.empty())
    {
        return false;
    }

    if (!_active_match_line.has_value())
    {
        return focus_match_at(_matching_lines.size() - 1);
    }

    const auto current = std::find(_matching_lines.begin(), _matching_lines.end(), *_active_match_line);
    if (current == _matching_lines.end())
    {
        return focus_match_at(_matching_lines.size() - 1);
    }

    const std::size_t current_position  = static_cast<std::size_t>(std::distance(_matching_lines.begin(), current));
    const std::size_t previous_position = current_position == 0 ? _matching_lines.size() - 1 : current_position - 1;
    return focus_match_at(previous_position);
}

std::optional<std::size_t> LogView2FindManager::consume_pending_focus_line()
{
    auto pending = _pending_focus_line;
    _pending_focus_line.reset();
    return pending;
}

void LogView2FindManager::on_data_changed(VisibleLineIndex first_changed_line)
{
    if (!active())
    {
        return;
    }

    rebuild_matches_from(static_cast<std::size_t>(std::max(0, first_changed_line.value)));
    if (_active_match_line.has_value() && !line_matches(*_active_match_line))
    {
        _active_match_line.reset();
    }
}

void LogView2FindManager::rebuild_matches()
{
    _matching_lines.clear();
    rebuild_matches_from(0);
}

void LogView2FindManager::rebuild_matches_from(std::size_t first_changed_line)
{
    if (_data == nullptr || !_pattern.has_value())
    {
        return;
    }

    _matching_lines.erase(std::remove_if(_matching_lines.begin(), _matching_lines.end(), [first_changed_line](std::size_t line_index) { return line_index >= first_changed_line; }), _matching_lines.end());
    for (std::size_t line_index = first_changed_line; line_index < _data->size(); ++line_index)
    {
        if (row_matches_query(line_index))
        {
            _matching_lines.push_back(line_index);
        }
    }
}

bool LogView2FindManager::row_matches_query(std::size_t line_index) const
{
    return _data != nullptr && _pattern.has_value() && matches_pattern(_data->to_string(line_index), *_pattern);
}

bool LogView2FindManager::focus_match_at(std::size_t match_position)
{
    if (match_position >= _matching_lines.size())
    {
        return false;
    }

    _active_match_line  = _matching_lines[match_position];
    _pending_focus_line = _active_match_line;
    return true;
}

} // namespace slayerlog
