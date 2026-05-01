#include "log_view.hpp"
#include "view_theme.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace slayerlog
{

namespace
{

struct RenderedRow
{
    std::string text;
    std::optional<TextViewStyle> style;
};

// Approximate viewport size for the first render before FTXUI's reflect() has measured
// the actual box. Breakdown: window border (2) + header (1) + separators (2) + status lines (3).
// The value 7 slightly underestimates, which is acceptable as a fallback for a single frame.
constexpr int window_chrome_height = 7;

int estimate_viewport_line_count(int measured, int screen_height)
{
    if (measured > 0)
    {
        return measured;
    }

    return std::max(1, screen_height - window_chrome_height);
}

std::string join(const std::vector<std::string>& items, const std::string& separator = ", ")
{
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
        {
            result += separator;
        }
        result += items[i];
    }
    return result;
}

void apply_style(ftxui::Canvas& canvas, int row, int col_start, int col_end, const TextViewStyle& style)
{
    for (int col = col_start; col < col_end; ++col)
    {
        canvas.Style(col * 2, row * 4,
                     [&style](ftxui::Cell& cell)
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
                     });
    }
}

ftxui::Element build_filter_status(const AllProcessedSources& processed_sources)
{
    ftxui::Elements parts;
    parts.push_back(theme::badge("FILTER", theme::label_filter_fg));

    const auto hidden_before  = processed_sources.hidden_before_line_number();
    const auto hidden_columns = processed_sources.hidden_columns();

    if (processed_sources.include_filters().empty() && processed_sources.exclude_filters().empty() && !hidden_before.has_value() && !hidden_columns.has_value())
    {
        parts.push_back(ftxui::text(" none") | ftxui::color(theme::muted));
        return ftxui::hbox(std::move(parts));
    }

    if (!processed_sources.include_filters().empty())
    {
        parts.push_back(ftxui::text(" in(" + join(processed_sources.include_filters()) + ")"));
    }

    if (!processed_sources.exclude_filters().empty())
    {
        parts.push_back(ftxui::text(" out(" + join(processed_sources.exclude_filters()) + ")"));
    }

    if (hidden_before.has_value())
    {
        parts.push_back(ftxui::text(" | before line " + std::to_string(*hidden_before)) | ftxui::color(theme::muted));
    }

    if (hidden_columns.has_value())
    {
        parts.push_back(ftxui::text(" | columns " + std::to_string(hidden_columns->start) + "-" + std::to_string(hidden_columns->end)) | ftxui::color(theme::muted));
    }

    return ftxui::hbox(std::move(parts));
}

ftxui::Element build_find_status(const AllProcessedSources& processed_sources, const LogController& controller)
{
    ftxui::Elements parts;
    parts.push_back(theme::badge("FIND", theme::label_find_fg));

    if (!controller.find_active())
    {
        parts.push_back(ftxui::text(" off") | ftxui::color(theme::muted));
        return ftxui::hbox(std::move(parts));
    }

    parts.push_back(ftxui::text(" \"" + controller.find_query() + "\""));
    parts.push_back(ftxui::text(" " + std::to_string(controller.visible_find_match_count(processed_sources)) + "/" + std::to_string(controller.total_find_match_count()) + " matches") | ftxui::color(theme::muted));

    const auto active_visible_index = controller.active_find_visible_index(processed_sources);
    if (active_visible_index.has_value())
    {
        parts.push_back(ftxui::text(" | line " + std::to_string(active_visible_index->value + 1)) | ftxui::color(theme::muted));
    }

    return ftxui::hbox(std::move(parts));
}

ftxui::Element build_key_hints()
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({
        theme::key_hint("Ctrl+P", "commands"),
        sep(),
        theme::key_hint("Ctrl+F", "find"),
        sep(),
        theme::key_hint("Ctrl+R", "history"),
        sep(),
        theme::key_hint("\xe2\x86\x92", "next"),
        sep(),
        theme::key_hint("\xe2\x86\x90", "prev"),
        sep(),
        theme::key_hint("Esc", "close find"),
        sep(),
        theme::key_hint("q", "quit"),
    });
}

} // namespace

ftxui::Element LogView::render(const AllProcessedSources& processed_sources, LogController& controller, const std::string& header_text, int screen_height, std::optional<HiddenColumnRange> hidden_column_preview)
{
    // Update viewport dimensions on the text view controller
    const int effective_height = estimate_viewport_line_count(_text_view.viewport_line_count(), screen_height);
    const int effective_width  = std::max(1, _text_view.viewport_col_count());
    controller.text_view_controller().update_viewport_line_count(effective_height);
    controller.text_view_controller().update_viewport_col_count(effective_width);

    // Get render data from the text view controller
    auto data              = controller.text_view_controller().render_data();
    const bool empty_state = processed_sources.line_count() == 0;
    std::vector<RenderedRow> rendered_rows;

    if (empty_state)
    {
        data.total_lines        = 1;
        data.first_visible_line = 0;
        data.first_visible_col  = 0;
        data.range_decorations.clear();
        data.max_line_width = static_cast<int>(2 + std::string(processed_sources.total_line_count() == 0 ? "<empty file>" : "<no matching lines>").size());

        RenderedRow row;
        row.text = "1 " + std::string(processed_sources.total_line_count() == 0 ? "<empty file>" : "<no matching lines>");

        TextViewStyle style;
        style.dim = true;
        row.style = style;
        rendered_rows.push_back(std::move(row));
    }
    else
    {
        const auto active_find_index = controller.active_find_visible_index(processed_sources);
        const int row_count          = std::max(0, std::min(data.total_lines - data.first_visible_line, data.viewport_line_count));
        const bool decorate_source_numbers = processed_sources.show_source_labels() && !processed_sources.hidden_columns().has_value();
        const int source_column_start      = processed_sources.source_number_column_start();
        const int source_column_end        = source_column_start + processed_sources.source_number_column_width();
        std::vector<TextViewRangeDecoration> source_decorations;
        source_decorations.reserve(static_cast<std::size_t>(row_count));
        rendered_rows.reserve(static_cast<std::size_t>(row_count));

        for (int row = 0; row < row_count; ++row)
        {
            const int line_index = data.first_visible_line + row;

            RenderedRow rendered_row;
            rendered_row.text = controller.line_at(line_index);

            const bool is_find_match  = controller.find_active() && controller.visible_line_matches_find(processed_sources, line_index);
            const bool is_active_find = active_find_index.has_value() && active_find_index->value == line_index;
            if (is_find_match)
            {
                TextViewStyle style;
                style.background   = is_active_find ? theme::find_active_bg : theme::find_match_bg;
                style.foreground   = is_active_find ? theme::find_active_fg : theme::find_match_fg;
                rendered_row.style = style;
            }

            if (decorate_source_numbers)
            {
                const auto entry_index = processed_sources.entry_index_for_visible_line(VisibleLineIndex {line_index});
                if (entry_index.has_value())
                {
                    const auto& entry = processed_sources.entry_at(*entry_index);

                    TextViewRangeDecoration source_decoration;
                    source_decoration.line_index = line_index;
                    source_decoration.col_start  = source_column_start;
                    source_decoration.col_end    = source_column_end;
                    source_decoration.style.foreground = theme::source_tag_color(entry.metadata.source_index);
                    source_decoration.style.bold       = true;
                    source_decorations.push_back(source_decoration);
                }
            }

            rendered_rows.push_back(std::move(rendered_row));
        }

        if (!source_decorations.empty())
        {
            data.range_decorations.insert(data.range_decorations.begin(), source_decorations.begin(), source_decorations.end());
        }
    }

    if (hidden_column_preview.has_value())
    {
        data.col_highlight.col_start = hidden_column_preview->start;
        data.col_highlight.col_end   = hidden_column_preview->end;
        data.col_highlight.color     = theme::hidden_columns_preview_bg;
        data.col_highlight.active    = true;
    }

    const auto draw_content = [rendered_rows = std::move(rendered_rows)](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        (void)first_line;

        const int rendered_line_count = std::min(line_count, static_cast<int>(rendered_rows.size()));
        for (int row = 0; row < rendered_line_count; ++row)
        {
            const auto& rendered_row = rendered_rows[static_cast<std::size_t>(row)];
            if (first_col < static_cast<int>(rendered_row.text.size()))
            {
                canvas.DrawText(0, row * 4, rendered_row.text.substr(static_cast<std::size_t>(first_col), static_cast<std::size_t>(col_count)));
            }

            if (!rendered_row.style.has_value())
            {
                continue;
            }

            const int visible_width = std::min(col_count, std::max(0, static_cast<int>(rendered_row.text.size()) - first_col));
            if (visible_width <= 0)
            {
                continue;
            }

            apply_style(canvas, row, 0, visible_width, *rendered_row.style);
        }
    };

    auto log_view = _text_view.render(data, draw_content) | ftxui::flex;

    // Header with optional paused indicator
    ftxui::Element header;
    if (processed_sources.updates_paused())
    {
        header = ftxui::hbox({
            ftxui::text(header_text) | ftxui::bold,
            ftxui::text(" "),
            theme::badge("PAUSED", theme::paused_fg),
        });
    }
    else
    {
        header = ftxui::text(header_text) | ftxui::bold;
    }

    return ftxui::window(ftxui::text("Slayerlog"), ftxui::vbox({
                                                       header,
                                                       ftxui::separator(),
                                                       log_view,
                                                       ftxui::separator(),
                                                       build_filter_status(processed_sources),
                                                       build_find_status(processed_sources, controller),
                                                       build_key_hints(),
                                                   }));
}

std::optional<TextViewPosition> LogView::mouse_to_text_position(const LogController& controller, const ftxui::Mouse& mouse) const
{
    const auto data = controller.text_view_controller().render_data();
    if (data.total_lines == 0)
    {
        return std::nullopt;
    }

    return _text_view.mouse_to_text_position(data, mouse);
}

} // namespace slayerlog
