#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command_history.hpp"
#include "commands/command_palette_session.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_command_palette_session_tests_" + unique_suffix + ".ini");
}

void remove_temp_settings_file(const std::filesystem::path& settings_path)
{
    std::error_code error_code;
    std::filesystem::remove(settings_path, error_code);
    std::filesystem::remove(settings_path.string() + ".tmp", error_code);

    const auto parent_path = settings_path.parent_path();
    if (parent_path.empty() || !std::filesystem::exists(parent_path, error_code))
    {
        return;
    }

    const std::string backup_prefix = settings_path.filename().string() + ".";
    for (const auto& entry : std::filesystem::directory_iterator(parent_path, error_code))
    {
        if (error_code)
        {
            return;
        }

        const auto filename = entry.path().filename().string();
        if (filename.rfind(backup_prefix, 0) == 0 && entry.path().extension() == ".bak")
        {
            std::filesystem::remove(entry.path(), error_code);
        }
    }
}

} // namespace

TEST(CommandPaletteSessionTest, OpenLoadsAllCommandsAndInsertTextFiltersMatches)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"beta", "Second", "beta"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"binary", "Third", "binary"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    int results_changed_count = 0;
    session.set_results_changed_callback([&results_changed_count] { ++results_changed_count; });

    session.open();

    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
    ASSERT_EQ(session.model().matching_commands.size(), 3U);
    EXPECT_GT(results_changed_count, 0);

    session.insert_text("b");

    EXPECT_EQ(session.model().query, "b");
    EXPECT_EQ(session.model().cursor_position, 1U);
    ASSERT_EQ(session.model().matching_commands.size(), 2U);
    EXPECT_EQ(session.model().matching_commands[0].name, "beta");
    EXPECT_EQ(session.model().matching_commands[1].name, "binary");
}

TEST(CommandPaletteSessionTest, MoveSelectionClampsAndFiresSelectionCallback)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"beta", "Second", "beta"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"gamma", "Third", "gamma"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);
    session.open();

    int selection_changed_count = 0;
    session.set_selection_changed_callback([&selection_changed_count] { ++selection_changed_count; });

    session.move_selection(1);
    EXPECT_EQ(session.model().selected_index, 1);
    EXPECT_EQ(selection_changed_count, 1);

    session.move_selection(10);
    EXPECT_EQ(session.model().selected_index, 2);

    session.move_selection(-10);
    EXPECT_EQ(session.model().selected_index, 0);
}

TEST(CommandPaletteSessionTest, HistoryModeToggleListsEntriesAndCopyToQueryReturnsToCommandMode)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));
    ASSERT_TRUE(history.record_command("beta first", error_message));
    ASSERT_TRUE(history.record_command("beta second", error_message));

    CommandManager manager;
    manager.register_command({"beta", "Second", "beta <args>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager, history);
    session.open();

    ASSERT_TRUE(session.toggle_history_mode());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::History);
    ASSERT_EQ(session.model().matching_history_entries.size(), 2U);
    EXPECT_EQ(session.model().matching_history_entries[0], "beta second");
    EXPECT_EQ(session.model().matching_history_entries[1], "beta first");

    session.insert_text("fir");
    ASSERT_EQ(session.model().matching_history_entries.size(), 1U);
    EXPECT_EQ(session.model().matching_history_entries[0], "beta first");

    ASSERT_TRUE(session.copy_selected_history_entry_to_query());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
    EXPECT_EQ(session.model().query, "beta first");
    EXPECT_EQ(session.model().cursor_position, std::string("beta first").size());
    EXPECT_EQ(session.model().selected_index, 0);

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteSessionTest, ToggleHistoryModeUnavailableWithoutCommandHistory)
{
    CommandManager manager;
    manager.register_command({"alpha", "First", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);
    session.open();

    EXPECT_FALSE(session.has_command_history());
    EXPECT_FALSE(session.toggle_history_mode());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
}

TEST(CommandPaletteSessionTest, SubmitExecutesSelectedCommandAndRecordsHistory)
{
    const auto settings_path = make_temp_settings_path();
    SettingsStore settings_store(settings_path);
    CommandHistory history(settings_store);
    std::string error_message;
    ASSERT_TRUE(history.load(error_message));

    CommandManager manager;
    std::string executed_arguments;
    manager.register_command({"beta", "Second", "beta <args>"},
                             [&](std::string_view arguments)
                             {
                                 executed_arguments = std::string(arguments);
                                 return CommandResult {true, "done"};
                             });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager, history);
    session.open();
    session.insert_text("beta hello");
    session.submit();

    EXPECT_EQ(executed_arguments, "hello");
    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "done");
    EXPECT_FALSE(session.model().status_is_error);
    ASSERT_EQ(history.entries().size(), 1U);
    EXPECT_EQ(history.entries()[0], "beta hello");

    remove_temp_settings_file(settings_path);
}

TEST(CommandPaletteSessionTest, AutocompleteReplacesCommandNameAndKeepsArguments)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-out", "Exclude matching lines", "filter-out <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);
    session.open();
    session.insert_text("fil error text");
    session.autocomplete_selected_command();

    EXPECT_EQ(session.model().query, "filter-in error text");
    EXPECT_EQ(session.model().cursor_position, std::string("filter-in").size());
}

TEST(CommandPaletteSessionTest, ClosePickerFlowConfirmsSelectedIndexAndCloses)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    int confirmed_index = -1;
    session.open_close_open_file_picker({"alpha.log", "beta.log", "gamma.log"},
                                        [&](std::size_t index)
                                        {
                                            confirmed_index = static_cast<int>(index);
                                            return CommandResult {true, "Closed"};
                                        });

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::CloseOpenFile);
    ASSERT_EQ(session.model().open_files.size(), 3U);

    session.move_selection(2);
    session.submit();

    EXPECT_EQ(confirmed_index, 2);
    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "Closed");
}

TEST(CommandPaletteSessionTest, HiddenColumnPreviewFollowsQuery)
{
    CommandManager manager;
    manager.register_command({"hide-columns", "Hide columns", "hide-columns <xx-yy>"}, [](std::string_view) { return CommandResult {true, "ok"}; }, [](std::string_view arguments) { return parse_hidden_column_range(arguments); });

    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);
    session.open();

    session.insert_text("hide-columns 4-10");
    ASSERT_TRUE(session.model().hidden_column_preview.has_value());
    EXPECT_EQ(*session.model().hidden_column_preview, (HiddenColumnRange {4, 10}));

    session.close();
    EXPECT_FALSE(session.model().hidden_column_preview.has_value());
}

TEST(CommandPaletteSessionTest, TimestampOffsetInputShowsPreviewAndConfirmsValidOffset)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    std::string confirmed_offset;
    session.open_timestamp_offset_input("alpha.log",
                                        [&](std::string_view offset_text)
                                        {
                                            confirmed_offset = std::string(offset_text);
                                            return CommandResult {true, "offset set"};
                                        });

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::EnterTimestampOffset);
    EXPECT_EQ(session.model().timestamp_offset_source_label, "alpha.log");
    EXPECT_EQ(session.model().timestamp_offset_preview, "Enter offset as DD hh:mm:ss[.fraction]");
    EXPECT_FALSE(session.model().timestamp_offset_preview_is_error);

    session.insert_text("20 02:10:10.005");
    EXPECT_EQ(session.model().timestamp_offset_preview, "Applies offset: +20d 02:10:10.005");
    EXPECT_FALSE(session.model().timestamp_offset_preview_is_error);

    session.submit();
    EXPECT_EQ(confirmed_offset, "20 02:10:10.005");
    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "offset set");
}

TEST(CommandPaletteSessionTest, TimestampOffsetInputRejectsInvalidOffsetAndStaysOpen)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    bool handler_called = false;
    session.open_timestamp_offset_input("alpha.log",
                                        [&](std::string_view)
                                        {
                                            handler_called = true;
                                            return CommandResult {true, "offset set"};
                                        });

    session.insert_text("bad");
    EXPECT_EQ(session.model().timestamp_offset_preview, "Invalid offset: expected DD hh:mm:ss[.fraction]");
    EXPECT_TRUE(session.model().timestamp_offset_preview_is_error);

    session.submit();
    EXPECT_TRUE(session.is_open());
    EXPECT_FALSE(handler_called);
    EXPECT_TRUE(session.model().status_is_error);
}

TEST(CommandPaletteSessionTest, DeleteFiltersMultiSelectConfirmPassesOnlySelectedEntries)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    std::vector<CommandPaletteModel::FilterPickerEntry> confirmed_filters;
    session.open_delete_filters_picker({{"alpha", true, 0, false}, {"beta", false, 0, false}, {"gamma", true, 1, false}},
                                       [&](const std::vector<CommandPaletteModel::FilterPickerEntry>& filters)
                                       {
                                           confirmed_filters = filters;
                                           return CommandResult {true, "Deleted"};
                                       });

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::DeleteFilters);

    session.toggle_selected_filter();
    session.move_selection(2);
    session.toggle_selected_filter();
    session.submit();

    ASSERT_EQ(confirmed_filters.size(), 2U);
    EXPECT_EQ(confirmed_filters[0].label, "alpha");
    EXPECT_EQ(confirmed_filters[1].label, "gamma");
    EXPECT_FALSE(session.is_open());
}

TEST(CommandPaletteSessionTest, DeleteFiltersSubmitWithoutSelectionFails)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    bool handler_called = false;
    session.open_delete_filters_picker({{"alpha", true, 0, false}},
                                       [&](const std::vector<CommandPaletteModel::FilterPickerEntry>&)
                                       {
                                           handler_called = true;
                                           return CommandResult {true, "Deleted"};
                                       });

    session.submit();

    EXPECT_TRUE(session.is_open());
    EXPECT_FALSE(handler_called);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "No filters are marked for deletion.");
}

TEST(CommandPaletteSessionTest, ChainedPickersKeepSessionOpenBetweenStages)
{
    CommandManager manager;
    CommandPaletteModel model;
    CommandPaletteSession session(model, manager);

    int selected_source_index = -1;
    session.open_timestamp_source_picker({"alpha.log", "beta.log"},
                                         [&](std::size_t index)
                                         {
                                             selected_source_index = static_cast<int>(index);
                                             session.open_timestamp_format_picker({"YYYY-MM-DDThh:mm:ss", "YYYY/MM/DD hh:mm:ss"}, [](std::size_t) { return CommandResult {true, "format set"}; });
                                             return CommandResult {true, "Pick format", false};
                                         });

    session.move_selection(1);
    session.submit();

    EXPECT_EQ(selected_source_index, 1);
    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::SelectTimestampFormat);

    session.move_selection(1);
    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "format set");
}

} // namespace slayerlog
