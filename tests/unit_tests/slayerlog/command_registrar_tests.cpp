#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "command_registrar.hpp"
#include "command_manager.hpp"
#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

std::filesystem::path make_temp_export_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_visible_export_" + unique_suffix + ".txt");
}

void remove_temp_export_file(const std::filesystem::path& export_path)
{
    std::error_code error_code;
    std::filesystem::remove(export_path, error_code);
}

std::string read_file_contents(const std::filesystem::path& export_path)
{
    std::ifstream input(export_path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string render_all_visible_lines(const AllProcessedSources& processed_sources)
{
    std::ostringstream output;
    for (int line_index = 0; line_index < processed_sources.line_count(); ++line_index)
    {
        if (line_index > 0)
        {
            output << '\n';
        }

        output << processed_sources.rendered_line(line_index);
    }

    return output.str();
}

} // namespace

TEST(CommandRegistrarTest, ExportVisibleTextWritesAllVisibleRenderedLines)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines({
        LogEntry {"alpha.log", "keep first"},
        LogEntry {"alpha.log", "drop second"},
        LogEntry {"alpha.log", "keep third"},
    });
    processed_sources.add_include_filter("keep");
    processed_sources.hide_columns(2, 5);
    ASSERT_EQ(processed_sources.line_count(), 2);

    CommandManager command_manager;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);

    const auto export_path = make_temp_export_path();
    const auto result      = command_manager.execute("export-visible-text " + export_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Exported 2 visible lines to " + export_path.string());
    EXPECT_EQ(read_file_contents(export_path), render_all_visible_lines(processed_sources));

    remove_temp_export_file(export_path);
}

TEST(CommandRegistrarTest, ShowAndHideOriginalTimeCommandsToggleRenderedMessage)
{
    AllProcessedSources processed_sources;
    LogEntry entry {"alpha.log", "INFO 2026-04-01 10:00:00 hello", std::nullopt, "2026-04-01 10:00:00"};
    entry.metadata.extracted_time_start = 5;
    entry.metadata.extracted_time_end   = 24;
    processed_sources.append_lines({entry});

    CommandManager command_manager;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);

    EXPECT_EQ(processed_sources.rendered_line(0), "1 {2026-04-01 10:00:00} INFO  hello");

    const auto show_result = command_manager.execute("show-original-time");
    EXPECT_TRUE(show_result.success);
    EXPECT_EQ(show_result.message, "Showing original detected timestamps in messages");
    EXPECT_EQ(processed_sources.rendered_line(0), "1 {2026-04-01 10:00:00} INFO 2026-04-01 10:00:00 hello");

    const auto hide_result = command_manager.execute("hide-original-time");
    EXPECT_TRUE(hide_result.success);
    EXPECT_EQ(hide_result.message, "Hiding original detected timestamps in messages");
    EXPECT_EQ(processed_sources.rendered_line(0), "1 {2026-04-01 10:00:00} INFO  hello");
}

TEST(CommandRegistrarTest, ShowAndHideIdenticalLinesCommandsToggleCollapsing)
{
    AllProcessedSources processed_sources;
    LogEntry first  {"alpha.log", "INFO 2026-04-01 10:00:00 hello"};
    LogEntry second {"alpha.log", "INFO 2026-04-01 10:00:01 hello"};
    first.metadata.extracted_time_start  = 5;
    first.metadata.extracted_time_end    = 24;
    second.metadata.extracted_time_start = 5;
    second.metadata.extracted_time_end   = 24;
    processed_sources.append_lines({first, second});

    CommandManager command_manager;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);

    ASSERT_EQ(processed_sources.line_count(), 2);
    EXPECT_EQ(processed_sources.rendered_line(1), "  hiding 1 identical messages above");

    const auto show_result = command_manager.execute("show-identical-lines");
    EXPECT_TRUE(show_result.success);
    EXPECT_EQ(show_result.message, "Showing identical messages");
    EXPECT_EQ(processed_sources.rendered_line(1), "2 INFO  hello");

    const auto hide_result = command_manager.execute("hide-identical-lines");
    EXPECT_TRUE(hide_result.success);
    EXPECT_EQ(hide_result.message, "Hiding identical messages");
    EXPECT_EQ(processed_sources.rendered_line(1), "  hiding 1 identical messages above");
}

TEST(CommandRegistrarTest, BuildHeaderTextIncludesNumberedSourceTags)
{
    EXPECT_EQ(build_header_text({}), "No files opened (use open-file <path> or open-folder <path>)");
    EXPECT_EQ(build_header_text({"file1.txt", "file2.txt"}), "file1.txt:1 | file2.txt:2");
    EXPECT_EQ(build_header_text({"single.log"}), "single.log:1");
}

TEST(CommandRegistrarTest, DeleteFiltersCommandOpensPickerAndRemovesSelectedFilters)
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

    CommandManager command_manager;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);

    const auto result = command_manager.execute("delete-filters");

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.close_palette_on_success);
    EXPECT_EQ(result.message, "Mark filters to delete and press Enter");
    ASSERT_TRUE(command_palette_controller.is_open());
    EXPECT_EQ(command_palette_controller.model().mode, CommandPaletteMode::DeleteFilters);
    ASSERT_EQ(command_palette_controller.model().filter_picker_entries.size(), 3U);
    EXPECT_EQ(command_palette_controller.model().filter_picker_entries[0].label, "show");
    EXPECT_EQ(command_palette_controller.model().filter_picker_entries[1].label, "gamma");
    EXPECT_EQ(command_palette_controller.model().filter_picker_entries[2].label, "beta");
    EXPECT_TRUE(command_palette_controller.model().filter_picker_entries[0].include);
    EXPECT_TRUE(command_palette_controller.model().filter_picker_entries[1].include);
    EXPECT_FALSE(command_palette_controller.model().filter_picker_entries[2].include);

    ASSERT_TRUE(command_palette_controller.handle_event(ftxui::Event::Character(" ")));
    ASSERT_TRUE(command_palette_controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(command_palette_controller.handle_event(ftxui::Event::ArrowDown));
    ASSERT_TRUE(command_palette_controller.handle_event(ftxui::Event::Character(" ")));
    ASSERT_TRUE(command_palette_controller.handle_event(ftxui::Event::Return));

    EXPECT_FALSE(command_palette_controller.is_open());
    EXPECT_EQ(processed_sources.include_filters().size(), 1U);
    EXPECT_EQ(processed_sources.include_filters()[0], "gamma");
    EXPECT_TRUE(processed_sources.exclude_filters().empty());
}

TEST(CommandRegistrarTest, DeleteFiltersCommandFailsWhenNoFiltersExist)
{
    AllProcessedSources processed_sources;

    CommandManager command_manager;
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);

    const auto result = command_manager.execute("delete-filters");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "No filters to delete");
}

} // namespace slayerlog
