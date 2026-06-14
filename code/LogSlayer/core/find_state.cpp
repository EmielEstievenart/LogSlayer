#include "find_state.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/log_entry_presentation.hpp"
#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

bool FindState::set_query(const AllProcessedSources& processed_sources, std::string query)
{
    query = trim_search_text(query);
    if (query.empty())
    {
        clear();
        return false;
    }

    const SearchPattern pattern = compile_search_pattern(query);

    _query   = pattern.raw_text;
    _pattern = pattern;
    rebuild_matches(processed_sources);
    _active_entry_index.reset();
    if (_match_entry_indices.empty())
    {
        return false;
    }

    return go_to_next_match(processed_sources);
}

void FindState::clear()
{
    _query.clear();
    _pattern.reset();
    _match_entry_indices.clear();
    _active_entry_index.reset();
}

bool FindState::active() const
{
    return !_query.empty();
}

const std::string& FindState::query() const
{
    return _query;
}

int FindState::total_match_count() const
{
    return static_cast<int>(_match_entry_indices.size());
}

int FindState::visible_match_count(const AllProcessedSources& processed_sources) const
{
    return static_cast<int>(std::count_if(_match_entry_indices.begin(), _match_entry_indices.end(), [&](AllLineIndex entry_index) { return processed_sources.entry_index_is_visible(entry_index); }));
}

bool FindState::visible_line_matches(const AllProcessedSources& processed_sources, int visible_index) const
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

    return std::binary_search(_match_entry_indices.begin(), _match_entry_indices.end(), *entry_index);
}

bool FindState::go_to_next_match(const AllProcessedSources& processed_sources)
{
    if (!active() || total_match_count() == 0)
    {
        return false;
    }

    int current_position = -1;
    if (_active_entry_index.has_value())
    {
        const auto position = std::find(_match_entry_indices.begin(), _match_entry_indices.end(), *_active_entry_index);
        if (position != _match_entry_indices.end())
        {
            current_position = static_cast<int>(std::distance(_match_entry_indices.begin(), position));
        }
    }

    for (int offset = 1; offset <= total_match_count(); ++offset)
    {
        const int next_position        = (current_position + offset) % total_match_count();
        const AllLineIndex entry_index = _match_entry_indices[FindResultIndex {next_position}];
        if (!processed_sources.entry_index_is_visible(entry_index))
        {
            continue;
        }

        _active_entry_index = entry_index;
        return true;
    }

    return false;
}

bool FindState::go_to_previous_match(const AllProcessedSources& processed_sources)
{
    if (!active() || total_match_count() == 0)
    {
        return false;
    }

    int current_position = 0;
    if (_active_entry_index.has_value())
    {
        const auto position = std::find(_match_entry_indices.begin(), _match_entry_indices.end(), *_active_entry_index);
        if (position != _match_entry_indices.end())
        {
            current_position = static_cast<int>(std::distance(_match_entry_indices.begin(), position));
        }
    }

    for (int offset = 1; offset <= total_match_count(); ++offset)
    {
        const int previous_position    = (current_position - offset + total_match_count()) % total_match_count();
        const AllLineIndex entry_index = _match_entry_indices[FindResultIndex {previous_position}];
        if (!processed_sources.entry_index_is_visible(entry_index))
        {
            continue;
        }

        _active_entry_index = entry_index;
        return true;
    }

    return false;
}

std::optional<AllLineIndex> FindState::active_entry_index() const
{
    return _active_entry_index;
}

std::optional<VisibleLineIndex> FindState::active_visible_index(const AllProcessedSources& processed_sources) const
{
    if (!_active_entry_index.has_value())
    {
        return std::nullopt;
    }

    return processed_sources.visible_line_index_for_entry(*_active_entry_index);
}

void FindState::rebuild_matches(const AllProcessedSources& processed_sources)
{
    _match_entry_indices.clear();
    if (!active())
    {
        return;
    }

    _match_entry_indices.reserve(static_cast<std::size_t>(processed_sources.total_line_count()));
    for (int index = 0; index < processed_sources.total_line_count(); ++index)
    {
        const AllLineIndex entry_index {index};
        if (entry_matches_query(processed_sources.entry_at(entry_index)))
        {
            _match_entry_indices.push_back(entry_index);
        }
    }
}

void FindState::expand_matches(const AllProcessedSources& processed_sources, AllLineIndex first_new_entry_index)
{
    if (!active())
    {
        return;
    }

    for (int index = first_new_entry_index.value; index < processed_sources.total_line_count(); ++index)
    {
        const AllLineIndex entry_index {index};
        if (entry_matches_query(processed_sources.entry_at(entry_index)))
        {
            _match_entry_indices.push_back(entry_index);
        }
    }
}

bool FindState::entry_matches_query(const LogEntry& entry) const
{
    return _pattern.has_value() && matches_pattern(presented_text(entry), *_pattern);
}

} // namespace slayerlog
