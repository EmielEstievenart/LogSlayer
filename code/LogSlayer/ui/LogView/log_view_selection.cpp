#include "log_view_selection.hpp"

#include "log_view_data.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
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

void LogViewSelection::begin(TextViewPosition position, const LogViewData& data)
{
    _anchor      = clamp(position, data);
    _focus       = _anchor;
    _in_progress = true;
}

void LogViewSelection::update(TextViewPosition position, const LogViewData& data)
{
    if (!_in_progress || !_anchor.has_value())
    {
        return;
    }

    _focus = clamp(position, data);
}

void LogViewSelection::end(std::optional<TextViewPosition> position, const LogViewData& data)
{
    _in_progress = false;
    if (position.has_value() && _anchor.has_value())
    {
        _focus = clamp(*position, data);
    }
}

void LogViewSelection::clear()
{
    _anchor.reset();
    _focus.reset();
    _in_progress = false;
}

bool LogViewSelection::in_progress() const
{
    return _in_progress;
}

std::string LogViewSelection::text(const LogViewData& data) const
{
    const auto selection = bounds(data);
    if (!selection.has_value())
    {
        return {};
    }

    const auto [start, end] = *selection;
    std::ostringstream output;
    for (int line_index = start.line_index; line_index <= end.line_index; ++line_index)
    {
        const auto line         = data.to_string(static_cast<std::size_t>(line_index));
        const int line_length   = static_cast<int>(line.size());
        const int line_start    = (line_index == start.line_index) ? start.column : 0;
        const int line_end      = (line_index == end.line_index) ? end.column : line_length;
        const int clamped_start = std::clamp(line_start, 0, line_length);
        const int clamped_end   = std::clamp(line_end, clamped_start, line_length);

        output << line.substr(static_cast<std::size_t>(clamped_start), static_cast<std::size_t>(clamped_end - clamped_start));
        if (line_index != end.line_index)
        {
            output << '\n';
        }
    }

    return output.str();
}

std::vector<TextViewRangeDecoration> LogViewSelection::decorations(const LogViewData& data) const
{
    std::vector<TextViewRangeDecoration> decorations;
    const auto selection = bounds(data);
    if (!selection.has_value())
    {
        return decorations;
    }

    const auto [start, end] = *selection;
    for (int line_index = start.line_index; line_index <= end.line_index; ++line_index)
    {
        const auto line           = data.to_string(static_cast<std::size_t>(line_index));
        const int line_length     = static_cast<int>(line.size());
        const int selection_start = (line_index == start.line_index) ? start.column : 0;
        const int selection_end   = (line_index == end.line_index) ? end.column : line_length;
        const int clamped_start   = std::clamp(selection_start, 0, line_length);
        const int clamped_end     = std::clamp(selection_end, clamped_start, line_length);
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

std::optional<std::pair<TextViewPosition, TextViewPosition>> LogViewSelection::bounds(const LogViewData& data) const
{
    if (!_anchor.has_value() || !_focus.has_value() || data.size() == 0)
    {
        return std::nullopt;
    }

    auto start = clamp(*_anchor, data);
    auto end   = clamp(*_focus, data);
    if (is_before(end, start))
    {
        std::swap(start, end);
    }

    return std::pair(start, end);
}

TextViewPosition LogViewSelection::clamp(TextViewPosition position, const LogViewData& data) const
{
    const int line_count = static_cast<int>(data.size());
    if (line_count == 0)
    {
        return TextViewPosition {0, 0};
    }

    position.line_index    = std::clamp(position.line_index, 0, line_count - 1);
    const auto line_length = static_cast<int>(data.to_string(static_cast<std::size_t>(position.line_index)).size());
    position.column        = std::clamp(position.column, 0, line_length);
    return position;
}

} // namespace slayerlog
