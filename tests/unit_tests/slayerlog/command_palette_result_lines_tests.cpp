#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "command_palette_model.hpp"
#include "command_palette_result_lines.hpp"

namespace slayerlog
{

TEST(CommandPaletteResultLinesTest, CommandModeRendersSummaryAndUsageLinePairs)
{
    CommandPaletteModel model;
    model.mode              = CommandPaletteMode::Commands;
    model.matching_commands = {
        {"filter-in", "Only show lines that match", "filter-in <text>"},
        {"go-to-line", "Jump to a line number", "go-to-line <number>"},
    };

    const auto result = build_command_palette_result_lines(model);

    const std::vector<std::string> expected_lines {
        "filter-in - Only show lines that match",
        "filter-in <text>",
        "go-to-line - Jump to a line number",
        "go-to-line <number>",
    };
    EXPECT_EQ(result.lines, expected_lines);
    EXPECT_EQ(result.entry_indices, (std::vector<int> {0, 0, 1, 1}));
}

TEST(CommandPaletteResultLinesTest, CommandModeWithoutMatchesRendersEmptyMessage)
{
    CommandPaletteModel model;
    model.mode  = CommandPaletteMode::Commands;
    model.query = "zzz";

    const auto result = build_command_palette_result_lines(model);

    EXPECT_EQ(result.lines, (std::vector<std::string> {"No matching commands"}));
    EXPECT_EQ(result.entry_indices, (std::vector<int> {-1}));
}

TEST(CommandPaletteResultLinesTest, HistoryModeRendersEntriesVerbatim)
{
    CommandPaletteModel model;
    model.mode                     = CommandPaletteMode::History;
    model.matching_history_entries = {"filter-in error", "go-to-line 42"};

    const auto result = build_command_palette_result_lines(model);

    EXPECT_EQ(result.lines, (std::vector<std::string> {"filter-in error", "go-to-line 42"}));
    EXPECT_EQ(result.entry_indices, (std::vector<int> {0, 1}));
}

TEST(CommandPaletteResultLinesTest, HistoryModeEmptyMessagesDependOnQuery)
{
    CommandPaletteModel model;
    model.mode = CommandPaletteMode::History;

    const auto without_query = build_command_palette_result_lines(model);
    EXPECT_EQ(without_query.lines, (std::vector<std::string> {"No previously run commands"}));
    EXPECT_EQ(without_query.entry_indices, (std::vector<int> {-1}));

    model.query           = "fil";
    const auto with_query = build_command_palette_result_lines(model);
    EXPECT_EQ(with_query.lines, (std::vector<std::string> {"No matching history commands"}));
    EXPECT_EQ(with_query.entry_indices, (std::vector<int> {-1}));
}

TEST(CommandPaletteResultLinesTest, CloseOpenFilePickerListsFilesOrEmptyMessage)
{
    CommandPaletteModel model;
    model.mode       = CommandPaletteMode::CloseOpenFile;
    model.open_files = {"alpha.log", "beta.log"};

    const auto with_files = build_command_palette_result_lines(model);
    EXPECT_EQ(with_files.lines, (std::vector<std::string> {"alpha.log", "beta.log"}));
    EXPECT_EQ(with_files.entry_indices, (std::vector<int> {0, 1}));

    model.open_files.clear();
    const auto without_files = build_command_palette_result_lines(model);
    EXPECT_EQ(without_files.lines, (std::vector<std::string> {"No open files"}));
    EXPECT_EQ(without_files.entry_indices, (std::vector<int> {-1}));
}

TEST(CommandPaletteResultLinesTest, TimestampSourcePickerUsesSourcesEmptyMessage)
{
    CommandPaletteModel model;
    model.mode = CommandPaletteMode::SelectTimestampSource;

    const auto result = build_command_palette_result_lines(model);

    EXPECT_EQ(result.lines, (std::vector<std::string> {"No open sources"}));
    EXPECT_EQ(result.entry_indices, (std::vector<int> {-1}));
}

TEST(CommandPaletteResultLinesTest, TimestampFormatPickerListsFormatsOrEmptyMessage)
{
    CommandPaletteModel model;
    model.mode              = CommandPaletteMode::SelectTimestampFormat;
    model.timestamp_formats = {"%Y-%m-%d %H:%M:%S", "%H:%M:%S"};

    const auto with_formats = build_command_palette_result_lines(model);
    EXPECT_EQ(with_formats.lines, (std::vector<std::string> {"%Y-%m-%d %H:%M:%S", "%H:%M:%S"}));
    EXPECT_EQ(with_formats.entry_indices, (std::vector<int> {0, 1}));

    model.timestamp_formats.clear();
    const auto without_formats = build_command_palette_result_lines(model);
    EXPECT_EQ(without_formats.lines, (std::vector<std::string> {"No timestamp formats configured"}));
    EXPECT_EQ(without_formats.entry_indices, (std::vector<int> {-1}));
}

TEST(CommandPaletteResultLinesTest, TimestampOffsetInputRendersHelpAndPreview)
{
    CommandPaletteModel model;
    model.mode                          = CommandPaletteMode::EnterTimestampOffset;
    model.timestamp_offset_source_label = "alpha.log";

    const auto without_preview = build_command_palette_result_lines(model);
    const std::vector<std::string> expected_without_preview {
        "Source: alpha.log",
        "Expected: DD hh:mm:ss[.fraction]",
        "Example: 20 02:10:10.005",
        "Enter an offset",
    };
    EXPECT_EQ(without_preview.lines, expected_without_preview);
    EXPECT_EQ(without_preview.entry_indices, (std::vector<int> {-1, -1, -1, -1}));

    model.timestamp_offset_preview = "Applies offset: 00 00:01:00";
    const auto with_preview        = build_command_palette_result_lines(model);
    EXPECT_EQ(with_preview.lines.back(), "Applies offset: 00 00:01:00");
}

TEST(CommandPaletteResultLinesTest, DeleteFiltersPickerRendersCheckboxAndDirectionTags)
{
    CommandPaletteModel model;
    model.mode                  = CommandPaletteMode::DeleteFilters;
    model.filter_picker_entries = {
        {"error", true, 0, true},
        {"noise", false, 1, false},
    };

    const auto result = build_command_palette_result_lines(model);

    EXPECT_EQ(result.lines, (std::vector<std::string> {"[x] (in) error", "[ ] (out) noise"}));
    EXPECT_EQ(result.entry_indices, (std::vector<int> {0, 1}));

    model.filter_picker_entries.clear();
    const auto without_filters = build_command_palette_result_lines(model);
    EXPECT_EQ(without_filters.lines, (std::vector<std::string> {"No filters configured"}));
    EXPECT_EQ(without_filters.entry_indices, (std::vector<int> {-1}));
}

} // namespace slayerlog
