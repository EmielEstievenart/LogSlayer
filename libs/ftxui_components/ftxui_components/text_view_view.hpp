#pragma once

#include <functional>
#include <optional>

#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/dom/canvas.hpp>

#include <ftxui_components/text_view_controller.hpp>

class TextViewView
{
public:
    using RenderCallback = std::function<void(ftxui::Canvas& canvas, int first_line_index, int line_count, int first_column, int column_count)>;

    TextViewView();

    [[nodiscard]] ftxui::Element render(const TextViewRenderData& data, const RenderCallback& draw_content);
    [[nodiscard]] int viewport_line_count() const;
    [[nodiscard]] int viewport_col_count() const;
    [[nodiscard]] std::optional<TextViewPosition> mouse_to_text_position(const TextViewRenderData& data, const ftxui::Mouse& mouse) const;

private:
    [[nodiscard]] TextViewRenderData normalize_render_data(TextViewRenderData data) const;

    ftxui::Box _box;
    ftxui::Box _content_box;

    ftxui::Element render_scrollbar(const TextViewRenderData& data);
    ftxui::Element render_hscrollbar(const TextViewRenderData& data);
};
