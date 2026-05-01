#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "commands/command_history.hpp"
#include "command_registrar.hpp"
#include "command_manager.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "log_controller.hpp"
#include "log_view.hpp"
#include "master_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

using LogModel = AllProcessedSources;

namespace
{

std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_master_controller_tests_" + unique_suffix + ".ini");
}

void remove_temp_settings_file(const std::filesystem::path& settings_path)
{
    std::error_code error_code;
    std::filesystem::remove(settings_path, error_code);
    std::filesystem::remove(settings_path.string() + ".tmp", error_code);
}

} // namespace

TEST(MasterControllerTest, CtrlPOpensCommandPalette)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::CtrlP));
    EXPECT_TRUE(command_palette_controller.is_open());
}

TEST(MasterControllerTest, CtrlFOpensFindPalettePrefilledFromSelection)
{
    LogModel model;
    LogController controller;
    model.append_lines({LogEntry {"alpha.log", "error before timeout after"}});
    controller.rebuild_view(model);

    const auto rendered_line   = model.rendered_line(0);
    const auto selection_start = rendered_line.find("timeout");
    ASSERT_NE(selection_start, std::string::npos);
    controller.text_view_controller().begin_selection(TextViewPosition {0, static_cast<int>(selection_start)});
    controller.text_view_controller().update_selection(TextViewPosition {0, static_cast<int>(selection_start + std::string("timeout").size())});
    controller.text_view_controller().end_selection(TextViewPosition {0, static_cast<int>(selection_start + std::string("timeout").size())});

    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::CtrlF));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::Commands);
    EXPECT_EQ(command_palette_controller.model().query, "find timeout");
}

TEST(MasterControllerTest, CtrlROpensHistoryPaletteWhenClosed)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::CtrlR));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::History);
}

TEST(MasterControllerTest, FindShortcutEnterActivatesFindAcrossAllMatches)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "error first"},
        LogEntry {"alpha.log", "info middle"},
        LogEntry {"alpha.log", "error second"},
    });
    controller.rebuild_view(model);

    const auto rendered_line   = model.rendered_line(0);
    const auto selection_start = rendered_line.find("error");
    ASSERT_NE(selection_start, std::string::npos);
    controller.text_view_controller().begin_selection(TextViewPosition {0, static_cast<int>(selection_start)});
    controller.text_view_controller().update_selection(TextViewPosition {0, static_cast<int>(selection_start + std::string("error").size())});
    controller.text_view_controller().end_selection(TextViewPosition {0, static_cast<int>(selection_start + std::string("error").size())});

    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, model, controller, command_palette_controller, header_text, screen, tracked_sources);
    LogView view;
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    ASSERT_TRUE(master_controller.handle_event(ftxui::Event::CtrlF));
    ASSERT_TRUE(master_controller.handle_event(ftxui::Event::Return));

    EXPECT_FALSE(command_palette_controller.is_open());
    EXPECT_TRUE(controller.find_active());
    EXPECT_EQ(controller.find_query(), "error");
    EXPECT_EQ(controller.total_find_match_count(), 2);
    EXPECT_EQ(controller.visible_find_match_count(model), 2);
    EXPECT_TRUE(controller.visible_line_matches_find(model, 0));
    EXPECT_FALSE(controller.visible_line_matches_find(model, 1));
    EXPECT_TRUE(controller.visible_line_matches_find(model, 2));
}

TEST(MasterControllerTest, WhenPaletteOpenInputRoutesToPalette)
{
    LogModel model;
    LogController controller;
    model.append_lines({
        LogEntry {"alpha.log", "line one"},
        LogEntry {"alpha.log", "line two"},
        LogEntry {"alpha.log", "line three"},
    });
    controller.rebuild_view(model);
    controller.text_view_controller().update_viewport_line_count(1);
    controller.text_view_controller().scroll_to_top();
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 0);

    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    command_manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    command_palette_controller.open();

    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::ArrowDown));
    EXPECT_EQ(controller.text_view_controller().first_visible_line(), 0);
}

TEST(MasterControllerTest, CtrlRTogglesHistoryModeWhilePaletteOpen)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("alpha one", error_message));

    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    command_manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    CommandPaletteController command_palette_controller(command_palette_model, command_manager, history);
    command_palette_controller.open();

    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::CtrlR));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::History);

    remove_temp_settings_file(settings_path);
}

TEST(MasterControllerTest, EscapeClosesPaletteBeforeExiting)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    command_palette_controller.open();

    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::Escape));
    EXPECT_FALSE(command_palette_controller.is_open());
    EXPECT_FALSE(master_controller.exit_requested());
}

TEST(MasterControllerTest, EscapeExitsWhenPaletteClosedAndFindInactive)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);

    LogView view;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    MasterController master_controller(model, controller, view, screen, command_palette_controller);

    EXPECT_TRUE(master_controller.handle_event(ftxui::Event::Escape));
    EXPECT_TRUE(master_controller.exit_requested());
}

} // namespace slayerlog
