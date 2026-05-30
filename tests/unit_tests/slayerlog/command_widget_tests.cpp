#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include <ftxui_components/text_view_component.hpp>

#include "command_widgets/editable_text.hpp"
#include "command_widgets/multi_select_list.hpp"
#include "command_widgets/single_select_list.hpp"
#include "command_widgets/text_input_panel.hpp"

namespace slayerlog
{

TEST(EditableTextTest, InsertsCharactersAndMovesCursor)
{
    EditableText input;

    ASSERT_TRUE(input.handle_event(ftxui::Event::Character("ab")));
    ASSERT_TRUE(input.handle_event(ftxui::Event::ArrowLeft));
    ASSERT_TRUE(input.handle_event(ftxui::Event::Character("X")));

    EXPECT_EQ(input.text(), "aXb");
    EXPECT_EQ(input.cursor_position(), 2U);
}

TEST(EditableTextTest, HomeEndBackspaceAndDeleteRespectUtf8Boundaries)
{
    EditableText input;
    input.set_text("a测b");

    ASSERT_TRUE(input.handle_event(ftxui::Event::ArrowLeft));
    ASSERT_TRUE(input.handle_event(ftxui::Event::Backspace));

    EXPECT_EQ(input.text(), "ab");
    EXPECT_EQ(input.cursor_position(), 1U);

    ASSERT_TRUE(input.handle_event(ftxui::Event::Home));
    ASSERT_TRUE(input.handle_event(ftxui::Event::Delete));

    EXPECT_EQ(input.text(), "b");
    EXPECT_EQ(input.cursor_position(), 0U);

    ASSERT_TRUE(input.handle_event(ftxui::Event::End));
    EXPECT_EQ(input.cursor_position(), 1U);
}

TEST(SingleSelectListTest, NavigatesAndClampsSelection)
{
    SingleSelectList list({"alpha", "beta", "gamma"});
    list.set_viewport_size(20, 2);

    ASSERT_TRUE(list.selected_index().has_value());
    EXPECT_EQ(*list.selected_index(), 0U);

    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowUp));
    EXPECT_EQ(*list.selected_index(), 0U);

    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowDown));

    EXPECT_EQ(*list.selected_index(), 2U);
    EXPECT_TRUE(list.handle_event(ftxui::Event::Return));
}

TEST(MultiSelectListTest, TogglesMultipleSelections)
{
    MultiSelectList list({"alpha", "beta", "gamma"});
    list.set_viewport_size(20, 2);

    ASSERT_TRUE(list.handle_event(ftxui::Event::Character(" ")));
    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(list.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(list.handle_event(ftxui::Event::Character(" ")));

    const std::vector<std::size_t> selected = list.selected_indices();
    ASSERT_EQ(selected.size(), 2U);
    EXPECT_EQ(selected[0], 0U);
    EXPECT_EQ(selected[1], 2U);
    EXPECT_TRUE(list.handle_event(ftxui::Event::Return));
}

TEST(TextViewComponentTest, SelectorStepSnapsViewportPageJumps)
{
    std::vector<std::string> lines;
    for (int index = 0; index < 15; ++index)
    {
        lines.push_back("line " + std::to_string(index));
    }

    TextViewComponentOption option;
    TextViewComponent view(std::move(option));
    view.set_selectable(true);
    view.set_selector_step(3);
    view.update_content_size(static_cast<int>(lines.size()), 7);
    view.controller().update_viewport_line_count(9);

    ASSERT_EQ(view.selected_line(), 0);

    view.page_selected_down();
    EXPECT_EQ(view.selected_line(), 9);

    view.page_selected_up();
    EXPECT_EQ(view.selected_line(), 0);

    view.select_next();
    EXPECT_EQ(view.selected_line(), 3);

    view.set_selected_line(12, true);
    view.page_selected_up();
    EXPECT_EQ(view.selected_line(), 3);

    view.page_selected_down();
    EXPECT_EQ(view.selected_line(), 12);

    view.select_last_line();
    EXPECT_EQ(view.selected_line(), 12);
}

TEST(TextViewComponentTest, SelectorStepKeepsFullSelectedBlockVisible)
{
    std::vector<std::string> lines;
    for (int index = 0; index < 16; ++index)
    {
        lines.push_back("line " + std::to_string(index));
    }

    TextViewComponentOption option;
    TextViewComponent view(std::move(option));
    view.set_selectable(true);
    view.set_selector_step(2);
    view.update_content_size(static_cast<int>(lines.size()), 7);
    view.controller().update_viewport_line_count(5);

    view.set_selected_line(14, true);

    const int visible_first = view.controller().first_visible_line();
    const int visible_last  = visible_first + view.controller().viewport_line_count() - 1;
    EXPECT_LE(visible_first, 14);
    EXPECT_GE(visible_last, 15);
}

TEST(TextInputPanelTest, EditsInputAndAcceptsPreviewState)
{
    TextInputPanel panel;

    ASSERT_TRUE(panel.handle_event(ftxui::Event::Character("20 02:10:10.005")));
    panel.set_preview("Applies offset: +20d 02:10:10.005", false);

    EXPECT_EQ(panel.text(), "20 02:10:10.005");

    panel.set_preview("Invalid offset", true);
    EXPECT_EQ(panel.text(), "20 02:10:10.005");
}

} // namespace slayerlog
