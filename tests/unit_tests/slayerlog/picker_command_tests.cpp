#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "command_manager.hpp"
#include "command_palette_model.hpp"
#include "command_palette_session.hpp"
#include "log_view_service.hpp"
#include "register_core_view_commands.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

// End-to-end tests for the five core picker commands: each one is executed
// through the CommandPaletteSession exactly like both UIs drive it (open the
// palette, type the command, submit), then the session's picker mode is
// asserted and the confirm path is driven with further session calls.

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_log_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_picker_command_" + unique_suffix + ".log");
}

void write_log_file(const std::filesystem::path& log_path, const std::string& content)
{
    std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << content;
}

void remove_temp_log_file(const std::filesystem::path& log_path)
{
    std::error_code error_code;
    std::filesystem::remove(log_path, error_code);
}

// Minimal LogViewService: the picker confirm handlers only need reload to
// rebuild the processed sources from the tracked sources.
class StubLogViewService : public LogViewService
{
public:
    void rebuild_view(const AllProcessedSources&) override { }
    void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) override { processed_sources.rebuild_from_sources(tracked_sources); }
    bool go_to_line(const AllProcessedSources&, int) override { return false; }
    int first_visible_line() const override { return 0; }
    int viewport_line_count() const override { return 0; }
    bool set_find_query(AllProcessedSources&, std::string) override { return false; }
    int total_find_match_count() const override { return 0; }
    int visible_find_match_count(const AllProcessedSources&) const override { return 0; }
    const std::string& find_query() const override { return _empty_query; }
    void start_time_alignment(TimeAlignmentApplyCallback) override { }

private:
    std::string _empty_query;
};

/// Runs a command by name the way both UIs do: open the palette, type the
/// command line, press Enter.
void run_command_from_palette(CommandPaletteSession& session, const std::string& command_line)
{
    session.open();
    session.insert_text(command_line);
    session.submit();
}

} // namespace

TEST(PickerCommandTest, DeleteFiltersOpensPickerAndDeletesMarkedFilters)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines({
        LogEntry {"alpha.log", "show keep alpha"},
        LogEntry {"alpha.log", "hide beta"},
        LogEntry {"alpha.log", "show gamma"},
    });
    processed_sources.add_include_filter("show");
    processed_sources.add_include_filter("gamma");
    processed_sources.add_exclude_filter("beta");

    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "delete-filters");

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::DeleteFilters);
    EXPECT_EQ(session.model().status_message, "Mark filters to delete and press Enter");
    ASSERT_EQ(session.model().filter_picker_entries.size(), 3U);
    EXPECT_EQ(session.model().filter_picker_entries[0].label, "show");
    EXPECT_TRUE(session.model().filter_picker_entries[0].include);
    EXPECT_EQ(session.model().filter_picker_entries[1].label, "gamma");
    EXPECT_EQ(session.model().filter_picker_entries[2].label, "beta");
    EXPECT_FALSE(session.model().filter_picker_entries[2].include);

    session.toggle_selected_filter();
    session.move_selection(2);
    session.toggle_selected_filter();
    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "Deleted 2 filters");
    ASSERT_EQ(processed_sources.include_filters().size(), 1U);
    EXPECT_EQ(processed_sources.include_filters()[0], "gamma");
    EXPECT_TRUE(processed_sources.exclude_filters().empty());
}

TEST(PickerCommandTest, DeleteFiltersSubmitWithoutMarksKeepsPickerOpen)
{
    AllProcessedSources processed_sources;
    processed_sources.add_include_filter("keep");

    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "delete-filters");
    session.submit();

    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::DeleteFilters);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "No filters are marked for deletion.");
    EXPECT_EQ(processed_sources.include_filters().size(), 1U);
}

TEST(PickerCommandTest, DeleteFiltersRejectsArgumentsAndFailsWithoutFilters)
{
    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "delete-filters unexpected");
    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "Usage: delete-filters");

    run_command_from_palette(session, "delete-filters");
    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "No filters to delete");
}

TEST(PickerCommandTest, CloseOpenFilePickerClosesSelectedSource)
{
    const auto log_path = make_temp_log_path();
    write_log_file(log_path, "plain line\n");

    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    const auto labels = tracked_sources.source_display_labels();
    ASSERT_EQ(labels.size(), 1U);

    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "close-open-file");

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::CloseOpenFile);
    EXPECT_EQ(session.model().status_message, "Select a file to close");
    EXPECT_EQ(session.model().open_files, labels);

    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message.rfind("Closed file: ", 0), 0U);
    EXPECT_EQ(tracked_sources.source_count(), 0U);
    EXPECT_EQ(processed_sources.line_count(), 0);

    remove_temp_log_file(log_path);
}

TEST(PickerCommandTest, CloseOpenFileFailsWithoutOpenFiles)
{
    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "close-open-file");

    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::Commands);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "No open files to close");
}

TEST(PickerCommandTest, SetTimeFormatRunsSourceThenFormatPickerAndAppliesFormat)
{
    const auto log_path = make_temp_log_path();
    write_log_file(log_path, "plain line\n");

    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    const auto labels  = tracked_sources.source_display_labels();
    const auto formats = tracked_sources.timestamp_formats();
    ASSERT_FALSE(formats.empty());

    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "set-time-format");

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::SelectTimestampSource);
    EXPECT_EQ(session.model().status_message, "Select a source to configure");
    EXPECT_EQ(session.model().open_files, labels);

    session.submit();

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::SelectTimestampFormat);
    EXPECT_EQ(session.model().status_message, "Select timestamp format for " + labels[0]);
    EXPECT_EQ(session.model().timestamp_formats, formats);

    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_FALSE(session.model().status_is_error);
    EXPECT_EQ(session.model().status_message, "Set timestamp format for " + labels[0] + ": " + formats[0]);

    remove_temp_log_file(log_path);
}

TEST(PickerCommandTest, AdjustTimeOffsetAccumulatesOffsetsAndDecoratesPickerLabels)
{
    const auto log_path = make_temp_log_path();
    write_log_file(log_path, "2026-04-01T10:00:00 alpha first\n");

    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    const auto labels = tracked_sources.source_display_labels();

    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "adjust-time-offset");

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::SelectTimestampSource);
    EXPECT_EQ(session.model().status_message, "Select a source to offset");
    ASSERT_EQ(session.model().open_files.size(), 1U);
    EXPECT_EQ(session.model().open_files[0], labels[0]);

    session.submit();

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::EnterTimestampOffset);
    EXPECT_EQ(session.model().status_message, "Enter timestamp offset for " + labels[0]);
    EXPECT_EQ(session.model().timestamp_offset_source_label, labels[0]);
    EXPECT_EQ(session.model().timestamp_offset_preview, "Enter offset as DD hh:mm:ss[.fraction]");

    session.insert_text("00 00:01:00");
    EXPECT_EQ(session.model().timestamp_offset_preview.rfind("Applies offset: ", 0), 0U);
    EXPECT_FALSE(session.model().timestamp_offset_preview_is_error);

    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message.rfind("Adjusted time offset for " + labels[0] + " by ", 0), 0U);
    {
        const auto offset = tracked_sources.source_timestamp_offset(0);
        ASSERT_TRUE(offset.has_value());
        EXPECT_EQ(offset->seconds, 60);
    }

    // Second round: the source picker now shows the current offset, and the
    // entered offset is added on top of the existing one.
    run_command_from_palette(session, "adjust-time-offset");
    ASSERT_EQ(session.model().open_files.size(), 1U);
    EXPECT_NE(session.model().open_files[0].find("[current "), std::string::npos);

    session.submit();
    EXPECT_NE(session.model().timestamp_offset_source_label.find("[current "), std::string::npos);

    session.insert_text("00 00:00:30.5");
    session.submit();

    EXPECT_FALSE(session.is_open());
    {
        const auto offset = tracked_sources.source_timestamp_offset(0);
        ASSERT_TRUE(offset.has_value());
        EXPECT_EQ(offset->seconds, 90);
        EXPECT_EQ(offset->nanosecond, 500000000);
    }

    remove_temp_log_file(log_path);
}

TEST(PickerCommandTest, AdjustTimeOffsetRejectsInvalidOffsetInput)
{
    const auto log_path = make_temp_log_path();
    write_log_file(log_path, "2026-04-01T10:00:00 alpha first\n");

    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());

    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "adjust-time-offset");
    session.submit();
    ASSERT_EQ(session.model().mode, CommandPaletteMode::EnterTimestampOffset);

    session.insert_text("bad");
    EXPECT_EQ(session.model().timestamp_offset_preview, "Invalid offset: expected DD hh:mm:ss[.fraction]");
    EXPECT_TRUE(session.model().timestamp_offset_preview_is_error);

    session.submit();

    EXPECT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::EnterTimestampOffset);
    EXPECT_TRUE(session.model().status_is_error);
    EXPECT_FALSE(tracked_sources.source_timestamp_offset(0).has_value());

    remove_temp_log_file(log_path);
}

TEST(PickerCommandTest, ClearTimeOffsetClearsSelectedSourceOffset)
{
    const auto log_path = make_temp_log_path();
    write_log_file(log_path, "2026-04-01T10:00:00 alpha first\n");

    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    const auto labels = tracked_sources.source_display_labels();

    const auto offset = parse_log_timestamp_offset("00 00:01:00");
    ASSERT_TRUE(offset.has_value());
    ASSERT_FALSE(tracked_sources.adjust_source_timestamp_offset(0, *offset).has_value());
    ASSERT_TRUE(tracked_sources.source_timestamp_offset(0).has_value());

    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession session(palette_model, command_manager);
    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, session);

    run_command_from_palette(session, "clear-time-offset");

    ASSERT_TRUE(session.is_open());
    EXPECT_EQ(session.model().mode, CommandPaletteMode::SelectTimestampSource);
    EXPECT_EQ(session.model().status_message, "Select a source to clear offset");
    EXPECT_EQ(session.model().open_files, labels);

    session.submit();

    EXPECT_FALSE(session.is_open());
    EXPECT_EQ(session.model().status_message, "Cleared time offset for " + labels[0]);
    EXPECT_FALSE(tracked_sources.source_timestamp_offset(0).has_value());

    remove_temp_log_file(log_path);
}

} // namespace slayerlog
