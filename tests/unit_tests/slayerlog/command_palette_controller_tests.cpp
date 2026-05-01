#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <ftxui/component/event.hpp>

#include "commands/command_history.hpp"
#include "command_palette_controller.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_command_palette_tests_" + unique_suffix + ".ini");
}

void remove_temp_settings_file(const std::filesystem::path& settings_path)
{
    std::error_code error_code;
    std::filesystem::remove(settings_path, error_code);
    std::filesystem::remove(settings_path.string() + ".tmp", error_code);
}

} // namespace

TEST(CommandPaletteControllerTest, OpenResetsSessionStateAndLoadsAllCommands)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"beta", "Second", "beta"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    model.open            = false;
    model.query           = "stale";
    model.cursor_position = 3;
    model.selected_index  = 10;
    model.status_message  = "error";
    model.status_is_error = true;

    CommandPaletteController controller(model, manager);
    controller.open();

    EXPECT_TRUE(controller.is_open());
    EXPECT_TRUE(controller.model().query.empty());
    EXPECT_EQ(controller.model().cursor_position, 0U);
    EXPECT_EQ(controller.model().selected_index, 0);
    EXPECT_TRUE(controller.model().status_message.empty());
    EXPECT_FALSE(controller.model().status_is_error);
    ASSERT_EQ(controller.model().matching_commands.size(), 2U);
    EXPECT_EQ(controller.model().matching_commands[0].name, "alpha");
    EXPECT_EQ(controller.model().matching_commands[1].name, "beta");
}

TEST(CommandPaletteControllerTest, OpenWithQueryPrefillsQueryAndRecomputesMatches)
{
    CommandManager manager;
    manager.register_command({"find", "Find text", "find <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-in", "Filter in", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open_with_query("find error");

    EXPECT_TRUE(controller.is_open());
    EXPECT_EQ(controller.model().query, "find error");
    EXPECT_EQ(controller.model().cursor_position, std::string("find error").size());
    ASSERT_EQ(controller.model().matching_commands.size(), 1U);
    EXPECT_EQ(controller.model().matching_commands[0].name, "find");
}

TEST(CommandPaletteControllerTest, CharacterInputUpdatesQueryAndMatchingCommands)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"beta", "Second", "beta"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    EXPECT_TRUE(controller.handle_event(ftxui::Event::Character('b')));

    EXPECT_EQ(controller.model().query, "b");
    EXPECT_EQ(controller.model().cursor_position, 1U);
    ASSERT_EQ(controller.model().matching_commands.size(), 1U);
    EXPECT_EQ(controller.model().matching_commands[0].name, "beta");
}

TEST(CommandPaletteControllerTest, ReturnExecutesSelectedCommandWithArgumentsAndClosesOnSuccess)
{
    CommandManager manager;
    std::string executed_arguments;
    manager.register_command({"beta", "Second", "beta <args>"},
                             [&](std::string_view arguments)
                             {
                                 executed_arguments = std::string(arguments);
                                 return CommandResult {true, "done"};
                             });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("beta hello world")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_EQ(executed_arguments, "hello world");
    EXPECT_FALSE(controller.is_open());
    EXPECT_EQ(controller.model().status_message, "done");
    EXPECT_FALSE(controller.model().status_is_error);
}

TEST(CommandPaletteControllerTest, ReturnKeepsPaletteOpenWhenCommandFails)
{
    CommandManager manager;
    manager.register_command({"beta", "Second", "beta"}, [](std::string_view) { return CommandResult {false, "failed"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("beta")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_TRUE(controller.is_open());
    EXPECT_EQ(controller.model().status_message, "failed");
    EXPECT_TRUE(controller.model().status_is_error);
}

TEST(CommandPaletteControllerTest, ReturnCanKeepPaletteOpenWhenCommandRequestsIt)
{
    CommandManager manager;
    manager.register_command({"stay-open", "Stay open", "stay-open"}, [](std::string_view) { return CommandResult {true, "pick one", false}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("stay-open")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_TRUE(controller.is_open());
    EXPECT_EQ(controller.model().status_message, "pick one");
    EXPECT_FALSE(controller.model().status_is_error);
}

TEST(CommandPaletteControllerTest, TabCompletesSelectedCommand)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-out", "Exclude matching lines", "filter-out <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("fil")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Tab));

    EXPECT_EQ(controller.model().query, "filter-in");
    EXPECT_EQ(controller.model().cursor_position, std::string("filter-in").size());
}

TEST(CommandPaletteControllerTest, TabPreservesExistingArguments)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-out", "Exclude matching lines", "filter-out <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("fil error text")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Tab));

    EXPECT_EQ(controller.model().query, "filter-in error text");
    EXPECT_EQ(controller.model().cursor_position, std::string("filter-in").size());
}

TEST(CommandPaletteControllerTest, TabLeavesQueryUnchangedWhenNoCommandsMatch)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("missing")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Tab));

    EXPECT_EQ(controller.model().query, "missing");
    EXPECT_EQ(controller.model().cursor_position, std::string("missing").size());
}

TEST(CommandPaletteControllerTest, TabClearsStatusMessage)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("fil")));
    model.status_message  = "stale";
    model.status_is_error = true;
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Tab));

    EXPECT_TRUE(controller.model().status_message.empty());
    EXPECT_FALSE(controller.model().status_is_error);
}

TEST(CommandPaletteControllerTest, CtrlArrowKeysScrollResultViewportHorizontally)
{
    CommandManager manager;
    manager.register_command({"very-long-command-name", "Summary", "very-long-command-name with long arguments and values"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();
    controller.result_text_view_controller().update_viewport_col_count(12);

    EXPECT_EQ(controller.result_text_view_controller().first_visible_col(), 0);
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowRightCtrl));
    EXPECT_GT(controller.result_text_view_controller().first_visible_col(), 0);

    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowLeftCtrl));
    EXPECT_EQ(controller.result_text_view_controller().first_visible_col(), 0);
}

TEST(CommandPaletteControllerTest, ArrowSelectionKeepsSelectedResultVisible)
{
    CommandManager manager;
    for (int index = 0; index < 10; ++index)
    {
        const std::string name = "command-" + std::to_string(index);
        manager.register_command({name, "summary", name + " <argument>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    }

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();
    controller.result_text_view_controller().update_viewport_line_count(3);

    for (int index = 0; index < 7; ++index)
    {
        ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowDown));
    }

    EXPECT_EQ(controller.model().selected_index, 7);
    EXPECT_GT(controller.result_text_view_controller().first_visible_line(), 0);
}

TEST(CommandPaletteControllerTest, OpenResetsResultViewportScrollOffsets)
{
    CommandManager manager;
    for (int index = 0; index < 8; ++index)
    {
        const std::string name = "command-" + std::to_string(index);
        manager.register_command({name, "summary", name + " with-very-long-argument-value"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    }

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();
    controller.result_text_view_controller().update_viewport_line_count(3);
    controller.result_text_view_controller().update_viewport_col_count(8);

    ASSERT_TRUE(controller.handle_event(ftxui::Event::PageDown));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowRightCtrl));
    EXPECT_GT(controller.result_text_view_controller().first_visible_line(), 0);
    EXPECT_GT(controller.result_text_view_controller().first_visible_col(), 0);

    controller.open();
    EXPECT_EQ(controller.result_text_view_controller().first_visible_line(), 0);
    EXPECT_EQ(controller.result_text_view_controller().first_visible_col(), 0);
}

TEST(CommandPaletteControllerTest, HideColumnsPreviewActivatesForValidRange)
{
    CommandManager manager;
    manager.register_command({"hide-columns", "Hide columns", "hide-columns <xx-yy>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("hide-columns 4-10")));

    ASSERT_TRUE(controller.model().hidden_column_preview.has_value());
    EXPECT_EQ(*controller.model().hidden_column_preview, (HiddenColumnRange {4, 10}));
}

TEST(CommandPaletteControllerTest, HideColumnsPreviewClearsForInvalidRangeAndOnClose)
{
    CommandManager manager;
    manager.register_command({"hide-columns", "Hide columns", "hide-columns <xx-yy>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("hide-columns 4-4")));
    EXPECT_FALSE(controller.model().hidden_column_preview.has_value());

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Backspace));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character('6')));
    ASSERT_TRUE(controller.model().hidden_column_preview.has_value());

    controller.close();
    EXPECT_FALSE(controller.model().hidden_column_preview.has_value());
}

TEST(CommandPaletteControllerTest, BackspaceAndDeleteRespectUtf8CodepointBoundaries)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("a测b")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowLeft));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Backspace));

    EXPECT_EQ(controller.model().query, "ab");
    EXPECT_EQ(controller.model().cursor_position, 1U);

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Delete));
    EXPECT_EQ(controller.model().query, "a");
    EXPECT_EQ(controller.model().cursor_position, 1U);
}

TEST(CommandPaletteControllerTest, ReturnExecutesRawQueryWhenNoMatchingCommands)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("missing-command value")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_TRUE(controller.is_open());
    EXPECT_TRUE(controller.model().status_is_error);
    EXPECT_EQ(controller.model().status_message, "Unknown command: missing-command");
}

TEST(CommandPaletteControllerTest, CtrlRTogglesHistoryMode)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("find error", error_message));
    ASSERT_TRUE(history.record_command("filter-in auth", error_message));

    CommandManager manager;
    manager.register_command({"find", "Find text", "find <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-in", "Filter in", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager, history);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::CtrlR));
    EXPECT_EQ(controller.model().mode, CommandPaletteMode::History);
    ASSERT_EQ(controller.model().matching_history_entries.size(), 2U);
    EXPECT_EQ(controller.model().matching_history_entries[0], "filter-in auth");
    EXPECT_EQ(controller.model().matching_history_entries[1], "find error");

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteControllerTest, CtrlRWithTextKeepsQueryAndTogglesHistoryMode)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("alpha --first", error_message));
    ASSERT_TRUE(history.record_command("beta second", error_message));

    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager, history);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character('a')));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::CtrlR));

    EXPECT_EQ(controller.model().mode, CommandPaletteMode::History);
    EXPECT_EQ(controller.model().query, "a");
    ASSERT_EQ(controller.model().matching_history_entries.size(), 2U);
    EXPECT_EQ(controller.model().matching_history_entries[0], "beta second");
    EXPECT_EQ(controller.model().matching_history_entries[1], "alpha --first");

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteControllerTest, HistoryModeFiltersAndExecutesSelectedCommand)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("beta first", error_message));
    ASSERT_TRUE(history.record_command("beta second", error_message));

    CommandManager manager;
    std::string executed_arguments;
    manager.register_command({"beta", "Second", "beta <args>"},
                             [&](std::string_view arguments)
                             {
                                 executed_arguments = std::string(arguments);
                                 return CommandResult {true, "done"};
                             });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager, history);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::CtrlR));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("sec")));
    ASSERT_EQ(controller.model().matching_history_entries.size(), 1U);
    EXPECT_EQ(controller.model().matching_history_entries[0], "beta second");

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));
    EXPECT_FALSE(controller.is_open());
    EXPECT_EQ(executed_arguments, "second");
    EXPECT_EQ(history.entries()[0], "beta second");

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteControllerTest, TabCopiesSelectedHistoryCommandToQueryForEditing)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("beta first", error_message));
    ASSERT_TRUE(history.record_command("beta second", error_message));

    CommandManager manager;
    manager.register_command({"beta", "Second", "beta <args>"}, [](std::string_view) { return CommandResult {true, "done"}; });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager, history);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::CtrlR));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("sec")));
    ASSERT_EQ(controller.model().matching_history_entries.size(), 1U);
    EXPECT_EQ(controller.model().matching_history_entries[0], "beta second");

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Tab));
    EXPECT_EQ(controller.model().mode, CommandPaletteMode::Commands);
    EXPECT_EQ(controller.model().query, "beta second");
    EXPECT_EQ(controller.model().cursor_position, std::string("beta second").size());

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteControllerTest, HistoryModeReturnExecutesTypedQueryWhenNoHistoryMatches)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("alpha previous", error_message));

    CommandManager manager;
    std::string executed_arguments;
    manager.register_command({"beta", "Second", "beta <args>"},
                             [&](std::string_view arguments)
                             {
                                 executed_arguments = std::string(arguments);
                                 return CommandResult {true, "done"};
                             });

    CommandPaletteModel model;
    CommandPaletteController controller(model, manager, history);
    controller.open();

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character("beta fresh")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::CtrlR));
    ASSERT_TRUE(controller.model().matching_history_entries.empty());

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));
    EXPECT_FALSE(controller.is_open());
    EXPECT_EQ(executed_arguments, "fresh");
    EXPECT_EQ(history.entries()[0], "beta fresh");

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteControllerTest, CloseOpenFilePickerSelectsAndExecutesHandler)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);

    int selected_index = -1;
    controller.open_close_open_file_picker({"alpha.log", "beta.log", "gamma.log"},
                                           [&](std::size_t index)
                                           {
                                               selected_index = static_cast<int>(index);
                                               return CommandResult {true, "Closed"};
                                           });

    ASSERT_TRUE(controller.is_open());
    EXPECT_EQ(controller.model().mode, CommandPaletteMode::CloseOpenFile);
    ASSERT_EQ(controller.model().open_files.size(), 3U);

    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_EQ(selected_index, 2);
    EXPECT_FALSE(controller.is_open());
}

TEST(CommandPaletteControllerTest, CloseOpenFilePickerEscapeCancels)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);

    bool handler_called = false;
    controller.open_close_open_file_picker({"alpha.log"},
                                           [&](std::size_t)
                                           {
                                               handler_called = true;
                                               return CommandResult {true, "Closed"};
                                           });

    ASSERT_TRUE(controller.is_open());
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Escape));

    EXPECT_FALSE(controller.is_open());
    EXPECT_FALSE(handler_called);
}

TEST(CommandPaletteControllerTest, DeleteFiltersPickerTogglesMultipleSelectionsAndExecutesHandler)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);

    std::vector<CommandPaletteModel::FilterPickerEntry> selected_filters;
    controller.open_delete_filters_picker({{"alpha", true, 0, false}, {"beta", false, 0, false}, {"gamma", true, 1, false}},
                                          [&](const std::vector<CommandPaletteModel::FilterPickerEntry>& filters)
                                          {
                                              selected_filters = filters;
                                              return CommandResult {true, "Deleted"};
                                          });

    ASSERT_TRUE(controller.is_open());
    EXPECT_EQ(controller.model().mode, CommandPaletteMode::DeleteFilters);
    ASSERT_EQ(controller.model().filter_picker_entries.size(), 3U);

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character(" ")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Character(" ")));
    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    ASSERT_EQ(selected_filters.size(), 2U);
    EXPECT_EQ(selected_filters[0].label, "alpha");
    EXPECT_TRUE(selected_filters[0].include);
    EXPECT_EQ(selected_filters[1].label, "gamma");
    EXPECT_TRUE(selected_filters[1].include);
    EXPECT_FALSE(controller.is_open());
}

TEST(CommandPaletteControllerTest, DeleteFiltersPickerRejectsEnterWhenNothingSelected)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteController controller(model, manager);

    bool handler_called = false;
    controller.open_delete_filters_picker({{"alpha", true, 0, false}},
                                          [&](const std::vector<CommandPaletteModel::FilterPickerEntry>&)
                                          {
                                              handler_called = true;
                                              return CommandResult {true, "Deleted"};
                                          });

    ASSERT_TRUE(controller.handle_event(ftxui::Event::Return));

    EXPECT_TRUE(controller.is_open());
    EXPECT_FALSE(handler_called);
    EXPECT_TRUE(controller.model().status_is_error);
    EXPECT_EQ(controller.model().status_message, "No filters are marked for deletion.");
}

} // namespace slayerlog
