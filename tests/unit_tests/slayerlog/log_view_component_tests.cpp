#include <gtest/gtest.h>

#include <mutex>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "command_manager.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "log_controller.hpp"
#include "log_view_component.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

using LogModel = AllProcessedSources;

// The command palette is opened by the left view component, but once open the
// modal capture lives in the top-level wiring (main.cpp), not in the component.
// These tests therefore cover only the component's own responsibilities:
// opening the palette via the shortcut keys, and exiting on Escape when nothing
// is open.

TEST(LogViewComponentTest, CtrlPOpensCommandPalette)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    std::string header_text;
    std::mutex model_mutex;
    LogViewComponent component(model, controller, command_palette_controller, screen, header_text, model_mutex);

    EXPECT_TRUE(component.OnEvent(ftxui::Event::CtrlP));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::Commands);
}

TEST(LogViewComponentTest, CtrlFOpensFindPalettePrefilledFromSelection)
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
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    std::string header_text;
    std::mutex model_mutex;
    LogViewComponent component(model, controller, command_palette_controller, screen, header_text, model_mutex);

    EXPECT_TRUE(component.OnEvent(ftxui::Event::CtrlF));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::Commands);
    EXPECT_EQ(command_palette_controller.model().query, "find timeout");
}

TEST(LogViewComponentTest, CtrlROpensHistoryPaletteWhenClosed)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    std::string header_text;
    std::mutex model_mutex;
    LogViewComponent component(model, controller, command_palette_controller, screen, header_text, model_mutex);

    EXPECT_TRUE(component.OnEvent(ftxui::Event::CtrlR));
    EXPECT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::History);
}

TEST(LogViewComponentTest, EscapeExitsWhenPaletteClosedAndFindInactive)
{
    LogModel model;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    std::string header_text;
    std::mutex model_mutex;
    LogViewComponent component(model, controller, command_palette_controller, screen, header_text, model_mutex);

    EXPECT_TRUE(component.OnEvent(ftxui::Event::Escape));
    EXPECT_TRUE(component.exit_requested());
    EXPECT_FALSE(command_palette_controller.is_open());
}

} // namespace slayerlog
