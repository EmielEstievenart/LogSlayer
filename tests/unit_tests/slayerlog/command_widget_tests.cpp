#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <ftxui/component/event.hpp>

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
