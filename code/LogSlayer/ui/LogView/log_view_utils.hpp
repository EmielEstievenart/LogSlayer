#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/canvas.hpp>
#include <ftxui/screen/color.hpp>

#include <ftxui_components/text_view_controller.hpp>

#include "log_view_data.hpp"
#include "log_view_selection.hpp"

namespace slayerlog
{

std::string trim_text(std::string_view text);

// Returns the trimmed selection text if it is non-empty and single-line,
// otherwise returns an empty string.
std::string selected_find_text(const LogViewSelection& selection, const LogViewData& data);

// Applies inverted-style decorations onto an already-drawn canvas, mapping
// each decoration's model-space columns into the visible viewport.
void draw_selection(ftxui::Canvas& canvas, const std::vector<TextViewRangeDecoration>& decorations, int first_line, int line_count, int first_col, int col_count);

void color_line(ftxui::Canvas& canvas, int row, int col_start, int col_end, ftxui::Color foreground, ftxui::Color background);

} // namespace slayerlog
