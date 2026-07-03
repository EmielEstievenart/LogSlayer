#include "log_view_utils.hpp"

#include <algorithm>
#include <cctype>

namespace slayerlog
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

std::string selected_find_text(const LogViewSelection& selection, const LogViewData& data)
{
    std::string trimmed = trim_text(selection.text(data));
    if (trimmed.find_first_of("\r\n") != std::string::npos)
    {
        return {};
    }
    return trimmed;
}

void draw_selection(ftxui::Canvas& canvas, const std::vector<TextViewRangeDecoration>& decorations, int first_line, int line_count, int first_col, int col_count)
{
    for (const auto& decoration : decorations)
    {
        const int row = decoration.line_index - first_line;
        if (row < 0 || row >= line_count)
        {
            continue;
        }

        const int start = std::max(0, decoration.col_start - first_col);
        const int end   = std::min(col_count, decoration.col_end - first_col);
        for (int col = start; col < end; ++col)
        {
            canvas.Style(col * 2, row * 4, [](ftxui::Cell& cell) { cell.inverted = true; });
        }
    }
}

void color_line(ftxui::Canvas& canvas, int row, int col_start, int col_end, ftxui::Color foreground, ftxui::Color background)
{
    for (int col = col_start; col < col_end; ++col)
    {
        canvas.Style(col * 2, row * 4,
                     [foreground, background](ftxui::Cell& cell)
                     {
                         cell.foreground_color = foreground;
                         cell.background_color = background;
                     });
    }
}

} // namespace slayerlog
