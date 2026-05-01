#include <ftxui_components/text_view_view.hpp>

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>

namespace
{

void invalidate_box(ftxui::Box& box)
{
    box.x_min = 0;
    box.x_max = -1;
    box.y_min = 0;
    box.y_max = -1;
}

int effective_viewport_line_count(const TextViewRenderData& data)
{
    return std::max(1, data.viewport_line_count);
}

int effective_viewport_col_count(const TextViewRenderData& data)
{
    return std::max(1, data.viewport_col_count);
}

int measured_viewport_extent(int min_value, int max_value)
{
    if (max_value < min_value)
    {
        return 0;
    }

    return max_value - min_value + 1;
}

void apply_style(ftxui::Cell& cell, const TextViewStyle& style)
{
    if (style.foreground.has_value())
    {
        cell.foreground_color = *style.foreground;
    }

    if (style.background.has_value())
    {
        cell.background_color = *style.background;
    }

    if (style.bold)
    {
        cell.bold = true;
    }

    if (style.dim)
    {
        cell.dim = true;
    }

    if (style.inverted)
    {
        cell.inverted = true;
    }
}

void style_columns(ftxui::Canvas& canvas, int row, int col_start, int col_end, const TextViewStyle& style)
{
    for (int col = col_start; col < col_end; ++col)
    {
        canvas.Style(col * 2, row * 4, [&](ftxui::Cell& cell) { apply_style(cell, style); });
    }
}

void style_highlighted_columns(ftxui::Canvas& canvas, int row, const TextViewRenderData& data)
{
    const TextViewColumnHighlight& hl = data.col_highlight;

    if (!hl.active || hl.col_start >= hl.col_end)
    {
        return;
    }

    const int vp_start = std::max(0, hl.col_start - data.first_visible_col);
    const int vp_end   = std::min(std::max(1, data.viewport_col_count), std::max(0, hl.col_end - data.first_visible_col));

    if (vp_start >= vp_end)
    {
        return;
    }

    TextViewStyle style;
    style.background = hl.color;
    style_columns(canvas, row, vp_start, vp_end, style);
}

void style_range_decorations(ftxui::Canvas& canvas, int row, const TextViewRenderData& data)
{
    const int line_index = data.first_visible_line + row;

    for (const TextViewRangeDecoration& decoration : data.range_decorations)
    {
        if (decoration.line_index != line_index || decoration.col_start >= decoration.col_end)
        {
            continue;
        }

        const int vp_start = std::max(0, decoration.col_start - data.first_visible_col);
        const int vp_end   = std::min(effective_viewport_col_count(data), std::max(0, decoration.col_end - data.first_visible_col));
        if (vp_start >= vp_end)
        {
            continue;
        }

        style_columns(canvas, row, vp_start, vp_end, decoration.style);
    }
}

ftxui::Element render_content(const TextViewRenderData& data, const TextViewView::RenderCallback& draw_content)
{
    const int viewport_lines = effective_viewport_line_count(data);
    const int viewport_cols  = effective_viewport_col_count(data);
    const int visible_lines  = std::max(0, std::min(data.total_lines - data.first_visible_line, viewport_lines));

    return ftxui::canvas(viewport_cols * 2, viewport_lines * 4,
                         [data, draw_content, visible_lines, viewport_cols](ftxui::Canvas& canvas)
                         {
                             for (int row = 0; row < visible_lines; ++row)
                             {
                                 style_highlighted_columns(canvas, row, data);
                             }

                             if (draw_content)
                             {
                                 draw_content(canvas, data.first_visible_line, visible_lines, data.first_visible_col, viewport_cols);
                             }

                             for (int row = 0; row < visible_lines; ++row)
                             {
                                 style_range_decorations(canvas, row, data);
                             }
                         });
}

} // namespace

TextViewView::TextViewView()
{
    invalidate_box(_box);
    invalidate_box(_content_box);
}

ftxui::Element TextViewView::render_scrollbar(const TextViewRenderData& data)
{
    const int viewport_lines = effective_viewport_line_count(data);

    if (data.total_lines <= viewport_lines)
    {
        return ftxui::text("");
    }

    const int thumb_height = std::max(1, (viewport_lines * viewport_lines) / std::max(data.total_lines, viewport_lines));
    const int max_offset   = std::max(1, data.total_lines - viewport_lines);

    // DrawBlock gives 2 block-rows per terminal row, enabling sub-character thumb positioning.
    const int total_block_rows   = viewport_lines * 2;
    const int thumb_h_blocks     = thumb_height * 2;
    const int track_range_blocks = std::max(0, total_block_rows - thumb_h_blocks);
    const int thumb_top_block    = (data.first_visible_line * track_range_blocks) / max_offset;

    return ftxui::canvas(2, viewport_lines * 4,
                         [thumb_top_block, thumb_h_blocks, total_block_rows](ftxui::Canvas& canvas)
                         {
                             for (int blk = 0; blk < total_block_rows; ++blk)
                             {
                                 const bool is_thumb      = blk >= thumb_top_block && blk < (thumb_top_block + thumb_h_blocks);
                                 const ftxui::Color color = is_thumb ? ftxui::Color::White : ftxui::Color::GrayDark;
                                 const int y              = blk * 2;
                                 canvas.DrawBlock(0, y, true, color);
                                 canvas.DrawBlock(1, y, true, color);
                             }
                         });
}

ftxui::Element TextViewView::render_hscrollbar(const TextViewRenderData& data)
{
    const int viewport_cols = std::max(1, data.viewport_col_count);

    if (data.max_line_width <= viewport_cols)
    {
        return ftxui::text("") | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 0);
    }

    const int thumb_width = std::max(1, (viewport_cols * viewport_cols) / std::max(data.max_line_width, viewport_cols));
    const int max_offset  = std::max(1, data.max_line_width - viewport_cols);

    // DrawBlock gives 2 block-columns per terminal column, enabling sub-character thumb positioning.
    const int total_block_cols   = viewport_cols * 2;
    const int thumb_w_blocks     = thumb_width * 2;
    const int track_range_blocks = std::max(0, total_block_cols - thumb_w_blocks);
    const int thumb_left_block   = (data.first_visible_col * track_range_blocks) / max_offset;

    // Use canvas height=2 (one block row) so the bar renders as upper half-blocks (▀),
    // matching the visual thickness of the 1-column-wide vertical scrollbar.
    return ftxui::canvas(viewport_cols * 2, 2,
                         [thumb_left_block, thumb_w_blocks, total_block_cols](ftxui::Canvas& canvas)
                         {
                             for (int blk = 0; blk < total_block_cols; ++blk)
                             {
                                 const bool is_thumb      = blk >= thumb_left_block && blk < (thumb_left_block + thumb_w_blocks);
                                 const ftxui::Color color = is_thumb ? ftxui::Color::White : ftxui::Color::GrayDark;
                                 canvas.DrawBlock(blk, 0, true, color);
                             }
                         });
}

TextViewRenderData TextViewView::normalize_render_data(TextViewRenderData data) const
{
    const int measured_height = viewport_line_count();
    const int measured_width  = viewport_col_count();

    data.viewport_line_count = measured_height > 0 ? measured_height : effective_viewport_line_count(data);
    data.viewport_col_count  = measured_width > 0 ? measured_width : effective_viewport_col_count(data);
    return data;
}

ftxui::Element TextViewView::render(const TextViewRenderData& input_data, const RenderCallback& draw_content)
{
    const TextViewRenderData data = normalize_render_data(input_data);
    return ftxui::vbox({
               ftxui::hbox({
                   render_content(data, draw_content) | ftxui::flex | ftxui::reflect(_content_box),
                   render_scrollbar(data),
               }) | ftxui::flex,
               render_hscrollbar(data),
           }) |
           ftxui::reflect(_box);
}

int TextViewView::viewport_line_count() const
{
    return measured_viewport_extent(_content_box.y_min, _content_box.y_max);
}

int TextViewView::viewport_col_count() const
{
    return measured_viewport_extent(_content_box.x_min, _content_box.x_max);
}

std::optional<TextViewPosition> TextViewView::mouse_to_text_position(const TextViewRenderData& input_data, const ftxui::Mouse& mouse) const
{
    const TextViewRenderData data = normalize_render_data(input_data);
    if (data.total_lines == 0 || viewport_line_count() == 0 || viewport_col_count() == 0)
    {
        return std::nullopt;
    }

    if (mouse.x < _content_box.x_min || mouse.x > _content_box.x_max || mouse.y < _content_box.y_min || mouse.y > _content_box.y_max)
    {
        return std::nullopt;
    }

    const int row        = mouse.y - _content_box.y_min;
    const int line_index = data.first_visible_line + row;
    if (row < 0 || row >= data.viewport_line_count || line_index < 0 || line_index >= data.total_lines)
    {
        return std::nullopt;
    }

    const int column = data.first_visible_col + std::max(0, mouse.x - _content_box.x_min);

    TextViewPosition position;
    position.line_index = line_index;
    position.column     = std::clamp(column, data.first_visible_col, data.max_line_width);
    return position;
}
