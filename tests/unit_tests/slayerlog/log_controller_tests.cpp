#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

using LogModel = AllProcessedSources;

namespace
{

std::vector<LogEntry> numbered_lines(int count)
{
    std::vector<LogEntry> lines;
    lines.reserve(static_cast<std::size_t>(count));
    for (int index = 1; index <= count; ++index)
    {
        lines.push_back({"alpha.log", "line " + std::to_string(index)});
    }

    return lines;
}

LogEntry timestamped_entry(std::string text)
{
    LogEntry entry {"alpha.log", std::move(text)};
    entry.metadata.extracted_time_start = 5;
    entry.metadata.extracted_time_end   = 24;
    return entry;
}

} // namespace

TEST(LogControllerTest, ScrollStateTracksViewportAndFollowBottom)
{
    LogModel model;
    LogController controller;
    model.append_lines(numbered_lines(10));
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(3);

    // follow_bottom is true by default, so first visible = 10 - 3 = 7
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 7);

    controller.text_view_controller().scroll_up(2);
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 5);

    // Appending with follow_bottom broken should not change scroll position
    model.append_lines(numbered_lines(2));
    controller.sync_view(model);
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 5);

    controller.text_view_controller().scroll_to_bottom();
    // 12 lines total, viewport 3: first visible = 12 - 3 = 9
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 9);
}

TEST(LogControllerTest, GoToLineCentersVisibleContentAndFailsForHiddenLine)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "info one"},
        LogEntry {"alpha.log", "error two"},
        LogEntry {"alpha.log", "info three"},
        LogEntry {"alpha.log", "info four"},
        LogEntry {"alpha.log", "error five"},
    });
    model.add_include_filter("error");
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    EXPECT_TRUE(controller.go_to_line(model, 5));
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 1);

    EXPECT_FALSE(controller.go_to_line(model, 1));
}

TEST(LogControllerTest, GoToLineTargetsCollapsedIdenticalRows)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        timestamped_entry("INFO 2026-04-01 10:00:00 hello"),
        timestamped_entry("INFO 2026-04-01 10:00:01 hello"),
        timestamped_entry("INFO 2026-04-01 10:00:02 hello"),
        timestamped_entry("INFO 2026-04-01 10:00:03 world"),
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.go_to_line(model, 2));
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 1);

    ASSERT_TRUE(controller.go_to_line(model, 3));
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 1);
}

TEST(LogControllerTest, FindNavigationUsesVisibleMatchesAndWraps)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error first"},
        LogEntry {"alpha.log", "error hidden"},
        LogEntry {"alpha.log", "error third"},
    });
    model.add_exclude_filter("hidden");
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.set_find_query(model, "error"));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 0);
    EXPECT_TRUE(controller.find_active());
    EXPECT_EQ(controller.total_find_match_count(), 3);
    EXPECT_EQ(controller.visible_find_match_count(model), 2);
    EXPECT_TRUE(controller.visible_line_matches_find(model, 0));
    EXPECT_FALSE(controller.visible_line_matches_find(model, 2));
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 0);

    EXPECT_TRUE(controller.go_to_next_find_match(model));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 1);
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 1);

    // Wraps around back to first match
    EXPECT_TRUE(controller.go_to_next_find_match(model));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 0);
}

TEST(LogControllerTest, FindNavigationRecoversWhenActiveMatchBecomesHidden)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error one"},
        LogEntry {"alpha.log", "error two"},
        LogEntry {"alpha.log", "error three"},
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.set_find_query(model, "error"));
    ASSERT_TRUE(controller.go_to_next_find_match(model));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 1);

    model.add_include_filter("three");
    controller.rebuild_view(model);
    EXPECT_FALSE(controller.active_find_visible_index(model).has_value());

    EXPECT_TRUE(controller.go_to_next_find_match(model));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 0);
    EXPECT_EQ(model.rendered_line(controller.active_find_visible_index(model)->value), "3 error three");
}

TEST(LogControllerTest, InvalidRegexFindKeepsExistingFindState)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error one"},
        LogEntry {"alpha.log", "error two"},
        LogEntry {"alpha.log", "info three"},
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.set_find_query(model, "error"));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    const auto active_before = controller.active_find_visible_index(model);

    EXPECT_THROW(controller.set_find_query(model, "re:["), std::invalid_argument);

    EXPECT_TRUE(controller.find_active());
    EXPECT_EQ(controller.find_query(), "error");
    EXPECT_EQ(controller.total_find_match_count(), 2);
    EXPECT_EQ(controller.active_find_visible_index(model), active_before);
}

TEST(LogControllerTest, HandleEventEscapeClearsFindBeforeRequestingExit)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error one"},
        LogEntry {"alpha.log", "error two"},
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.set_find_query(model, "error"));
    ASSERT_TRUE(controller.find_active());

    const auto result = controller.handle_event(model, ftxui::Event::Escape, {});

    EXPECT_TRUE(result.handled);
    EXPECT_FALSE(result.request_exit);
    EXPECT_FALSE(controller.find_active());
    EXPECT_EQ(controller.total_find_match_count(), 0);
}

TEST(LogControllerTest, HandleEventLeftAndRightArrowNavigateFindResults)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error one"},
        LogEntry {"alpha.log", "error two"},
        LogEntry {"alpha.log", "error three"},
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);

    ASSERT_TRUE(controller.set_find_query(model, "error"));
    ASSERT_TRUE(controller.go_to_next_find_match(model));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 1);

    const auto previous_result = controller.handle_event(model, ftxui::Event::ArrowLeft, {});
    EXPECT_TRUE(previous_result.handled);
    EXPECT_FALSE(previous_result.request_exit);
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 0);

    const auto next_result = controller.handle_event(model, ftxui::Event::ArrowRight, {});
    EXPECT_TRUE(next_result.handled);
    EXPECT_FALSE(next_result.request_exit);
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());
    EXPECT_EQ(controller.active_find_visible_index(model)->value, 1);
}

TEST(LogControllerTest, SelectionTracksBoundsAndExtractsText)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "alpha"},
        LogEntry {"alpha.log", "bravo"},
    });
    controller.rebuild_view(model);

    const auto first  = model.rendered_line(0);
    const auto second = model.rendered_line(1);

    controller.text_view_controller().begin_selection(TextViewPosition {0, 2});
    EXPECT_TRUE(controller.text_view_controller().selection_in_progress());
    controller.text_view_controller().update_selection(TextViewPosition {1, 3});
    controller.text_view_controller().end_selection(TextViewPosition {1, 3});

    EXPECT_FALSE(controller.text_view_controller().selection_in_progress());
    ASSERT_TRUE(controller.text_view_controller().selection_bounds().has_value());
    const auto bounds = *controller.text_view_controller().selection_bounds();
    EXPECT_EQ(bounds.first.line_index, 0);
    EXPECT_EQ(bounds.first.column, 2);
    EXPECT_EQ(bounds.second.line_index, 1);
    EXPECT_EQ(bounds.second.column, 3);

    EXPECT_EQ(controller.text_view_controller().selection_text(), first.substr(2) + "\n" + second.substr(0, 3));

    controller.text_view_controller().clear_selection();
    EXPECT_FALSE(controller.text_view_controller().selection_bounds().has_value());
    EXPECT_TRUE(controller.text_view_controller().selection_text().empty());
}

TEST(LogControllerTest, ResetClearsControllerState)
{
    LogModel model;
    LogController controller;
    model.append_lines(numbered_lines(6));
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(2);

    controller.text_view_controller().scroll_to_top();
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 0);

    ASSERT_TRUE(controller.set_find_query(model, "line"));
    ASSERT_TRUE(controller.active_find_visible_index(model).has_value());

    controller.text_view_controller().begin_selection(TextViewPosition {0, 1});
    controller.text_view_controller().update_selection(TextViewPosition {1, 2});
    EXPECT_TRUE(controller.text_view_controller().selection_in_progress());

    controller.reset();

    // After reset, buffers are cleared (0 lines), follow_bottom restored
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 0);
    EXPECT_TRUE(controller.text_view_controller().follow_bottom());
    EXPECT_FALSE(controller.active_find_visible_index(model).has_value());
    EXPECT_FALSE(controller.find_active());
    EXPECT_EQ(controller.total_find_match_count(), 0);
    EXPECT_FALSE(controller.text_view_controller().selection_in_progress());
    EXPECT_FALSE(controller.text_view_controller().selection_bounds().has_value());
    EXPECT_TRUE(controller.text_view_controller().selection_text().empty());
}

TEST(LogControllerTest, FindCountsAllMatchesWhileVisibleCountRespectsFilters)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error first"},
        LogEntry {"alpha.log", "error hidden"},
        LogEntry {"alpha.log", "error third"},
    });
    model.add_exclude_filter("hidden");
    controller.rebuild_view(model);

    EXPECT_TRUE(controller.set_find_query(model, "error"));
    EXPECT_EQ(controller.total_find_match_count(), 3);
    EXPECT_EQ(controller.visible_find_match_count(model), 2);
    EXPECT_FALSE(model.visible_line_index_for_line_number(2).has_value());
}

TEST(LogControllerTest, FindSupportsRegexWithRePrefix)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "request id=12"},
        LogEntry {"alpha.log", "request id=AB"},
        LogEntry {"alpha.log", "request id=77"},
    });
    controller.rebuild_view(model);

    EXPECT_TRUE(controller.set_find_query(model, "re:id=\\d+"));
    EXPECT_EQ(controller.find_query(), "re:id=\\d+");
    EXPECT_EQ(controller.total_find_match_count(), 2);
    EXPECT_EQ(controller.visible_find_match_count(model), 2);
    EXPECT_TRUE(controller.visible_line_matches_find(model, 0));
    EXPECT_FALSE(controller.visible_line_matches_find(model, 1));
    EXPECT_TRUE(controller.visible_line_matches_find(model, 2));
}

TEST(LogControllerTest, ClearingFindRemovesAllFindState)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "needle"},
    });
    controller.rebuild_view(model);

    ASSERT_TRUE(controller.set_find_query(model, "needle"));
    ASSERT_TRUE(controller.find_active());

    controller.clear_find(model);

    EXPECT_FALSE(controller.find_active());
    EXPECT_EQ(controller.total_find_match_count(), 0);
    EXPECT_EQ(controller.visible_find_match_count(model), 0);
}

} // namespace slayerlog
