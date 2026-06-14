#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "command.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "command_registrar.hpp"
#include "command_manager.hpp"
#include "command_support.hpp"
#include "implementations/log_view1/copy_settings_path_command.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

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

// Minimal LogViewService for command tests: commands only need *a* view service
// to build their CommandContext. The one behaviour the tests rely on is reload
// rebuilding the processed sources from the tracked sources (after open/close);
// everything else is a no-op. The real view rendering is covered elsewhere.
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

class RecordingNotificationSink : public NotificationSink
{
public:
    NotificationId show(Notification notification) override
    {
        notifications.push_back(std::move(notification));
        return next_id++;
    }

    void update(NotificationId id, Notification notification) override
    {
        updated_ids.push_back(id);
        notifications.push_back(std::move(notification));
    }

    void dismiss(NotificationId id) override { dismissed_ids.push_back(id); }

    NotificationId next_id = 1;
    std::vector<Notification> notifications;
    std::vector<NotificationId> updated_ids;
    std::vector<NotificationId> dismissed_ids;
};

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

LogTimestamp test_timestamp(unsigned second = 0)
{
    return *make_log_timestamp_utc(2026, 4, 1, 10, 0, second, 0);
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
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

    const auto export_path = make_temp_export_path();
    const auto result      = command_manager.execute("export-visible-text " + export_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Exported 2 visible lines to " + export_path.string());
    EXPECT_EQ(read_file_contents(export_path), render_all_visible_lines(processed_sources));

    remove_temp_export_file(export_path);
}

TEST(CommandRegistrarTest, CopySettingsPathCommandCopiesConfiguredSettingsPath)
{
    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    const auto settings_path = std::filesystem::temp_directory_path() / "slayerlog_settings_test.ini";

    std::string copied_text;
    CopySettingsPathCommand command({processed_sources, log_view, tracked_sources, {}, nullptr, nullptr, settings_path},
                                    [&](const std::string& text, std::string&)
                                    {
                                        copied_text = text;
                                        return true;
                                    });

    const auto result = command.execute("");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(copied_text, settings_path.string());
    EXPECT_EQ(result.message, "Copied settings path to clipboard: " + settings_path.string());
}

TEST(CommandRegistrarTest, CopySettingsPathCommandRejectsArguments)
{
    AllProcessedSources processed_sources;
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;

    CopySettingsPathCommand command({processed_sources, log_view, tracked_sources}, [](const std::string&, std::string&) { return true; });

    const auto result = command.execute("unexpected");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Usage: copy-settings-path");
}

TEST(CommandRegistrarTest, ShowAndHideOriginalTimeCommandsToggleRenderedMessage)
{
    AllProcessedSources processed_sources;
    LogEntry entry {"alpha.log", "INFO 2026-04-01 10:00:00 hello", test_timestamp()};
    entry.metadata.extracted_time_start = 5;
    entry.metadata.extracted_time_end   = 24;
    processed_sources.append_lines({entry});

    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

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
    LogEntry first {"alpha.log", "INFO 2026-04-01 10:00:00 hello"};
    LogEntry second {"alpha.log", "INFO 2026-04-01 10:00:01 hello"};
    first.metadata.extracted_time_start  = 5;
    first.metadata.extracted_time_end    = 24;
    second.metadata.extracted_time_start = 5;
    second.metadata.extracted_time_end   = 24;
    processed_sources.append_lines({first, second});

    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

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

TEST(CommandRegistrarTest, OpenFileCommandShowsToastWhenSourceAlreadyOpen)
{
    const auto log_path = make_temp_export_path();
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "plain line\n";
    }

    AllProcessedSources processed_sources;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, log_view, tracked_sources, Notifier(sink)});

    const auto result = command_manager.execute("open-file " + log_path.string());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Source already open: " + log_path.string());
    ASSERT_EQ(sink->notifications.size(), 1U);
    EXPECT_EQ(sink->notifications[0].title, "File already open");
    EXPECT_EQ(sink->notifications[0].message, log_path.filename().string());
    EXPECT_EQ(sink->notifications[0].level, NotificationLevel::Warning);

    remove_temp_export_file(log_path);
}

TEST(CommandRegistrarTest, OpenFileCommandOpensFileInBackgroundWhenTaskRunnerExists)
{
    const auto log_path = make_temp_export_path();
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "plain line\n";
    }

    AllProcessedSources processed_sources;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    std::mutex model_mutex;
    std::vector<std::thread> background_tasks;
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, log_view, tracked_sources, Notifier(sink), &model_mutex, &background_tasks});

    const auto result = command_manager.execute("open-file " + log_path.string());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "Opening file in background: " + log_path.string());
    ASSERT_EQ(background_tasks.size(), 1U);
    background_tasks.front().join();

    EXPECT_EQ(tracked_sources.line_count(), 1);
    ASSERT_EQ(processed_sources.line_count(), 1);
    EXPECT_NE(processed_sources.rendered_line(0).find("plain line"), std::string::npos);
    ASSERT_GE(sink->notifications.size(), 2U);
    bool saw_building_view = false;
    bool saw_file_opened   = false;
    for (const auto& notification : sink->notifications)
    {
        if (notification.title == "Building log view")
        {
            saw_building_view = true;
        }
        if (notification.title == "File opened" && notification.message == log_path.string() && notification.level == NotificationLevel::Success)
        {
            saw_file_opened = true;
        }
    }
    EXPECT_TRUE(saw_building_view);
    EXPECT_TRUE(saw_file_opened);

    remove_temp_export_file(log_path);
}

TEST(CommandRegistrarTest, OpenFolderCommandPreflightsAlreadyOpenSourceBeforeStartingBackgroundTask)
{
    const auto folder_path = std::filesystem::temp_directory_path() / ("slayerlog_command_folder_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder_path);
    const auto log_path = folder_path / "alpha.log";
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "plain line\n";
    }

    AllProcessedSources processed_sources;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, make_local_folder_source(folder_path.string())).has_value());
    std::mutex model_mutex;
    std::vector<std::thread> background_tasks;
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, log_view, tracked_sources, Notifier(sink), &model_mutex, &background_tasks});

    const auto result = command_manager.execute("open-folder " + folder_path.string());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Source already open: " + folder_path.string());
    EXPECT_TRUE(background_tasks.empty());
    ASSERT_EQ(sink->notifications.size(), 1U);
    EXPECT_EQ(sink->notifications[0].title, "Folder already open");
    EXPECT_EQ(sink->notifications[0].message, folder_path.filename().string());
    EXPECT_EQ(sink->notifications[0].level, NotificationLevel::Warning);

    std::error_code error_code;
    std::filesystem::remove_all(folder_path, error_code);
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
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

    const auto result = command_manager.execute("delete-filters");

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.close_palette_on_success);
    EXPECT_EQ(result.message, "Mark filters to delete and press Enter");
    ASSERT_NE(command_manager.active_command(), nullptr);

    ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Character(" ")).handled);
    ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::ArrowDown).handled);
    ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::ArrowDown).handled);
    ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Character(" ")).handled);
    const auto delete_result = dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Return);

    ASSERT_TRUE(delete_result.result.has_value());
    EXPECT_TRUE(delete_result.result->success);
    EXPECT_EQ(processed_sources.include_filters().size(), 1U);
    EXPECT_EQ(processed_sources.include_filters()[0], "gamma");
    EXPECT_TRUE(processed_sources.exclude_filters().empty());
}

TEST(CommandRegistrarTest, DeleteFiltersCommandFailsWhenNoFiltersExist)
{
    AllProcessedSources processed_sources;

    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

    const auto result = command_manager.execute("delete-filters");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "No filters to delete");
}

TEST(CommandRegistrarTest, SetTimeFormatCommandOpensSourceAndFormatPickers)
{
    const auto log_path = make_temp_export_path();
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "plain line\n";
    }

    AllProcessedSources processed_sources;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

    const auto result = command_manager.execute("set-time-format");

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.close_palette_on_success);
    EXPECT_EQ(result.message, "Select a source to configure");
    ASSERT_NE(command_manager.active_command(), nullptr);

    const auto source_result = dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Return);
    ASSERT_TRUE(source_result.result.has_value());
    EXPECT_TRUE(source_result.result->success);
    EXPECT_FALSE(source_result.result->close_palette_on_success);
    EXPECT_NE(source_result.result->message.find("Select timestamp format for "), std::string::npos);

    remove_temp_export_file(log_path);
}

TEST(CommandRegistrarTest, AdjustTimeOffsetCommandAccumulatesOffsets)
{
    const auto log_path = make_temp_export_path();
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "2026-04-01T10:00:00 alpha first\n";
    }

    AllProcessedSources processed_sources;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    StubLogViewService log_view;
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_path.string())).has_value());
    register_commands(command_manager, {processed_sources, log_view, tracked_sources});

    const auto apply_offset = [&](const std::string& offset_text)
    {
        const auto result = command_manager.execute("adjust-time-offset");
        ASSERT_TRUE(result.success);
        ASSERT_NE(command_manager.active_command(), nullptr);

        ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Return).handled);
        for (const char character : offset_text)
        {
            ASSERT_TRUE(dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Character(std::string(1, character))).handled);
        }

        const auto apply_result = dynamic_cast<Command*>(command_manager.active_command())->handle_event(ftxui::Event::Return);
        ASSERT_TRUE(apply_result.result.has_value());
        ASSERT_TRUE(apply_result.result->success);
        command_manager.clear_active_command();
    };

    apply_offset("00 00:01:00");
    {
        const auto offset = tracked_sources.source_timestamp_offset(0);
        ASSERT_TRUE(offset.has_value());
        EXPECT_EQ(offset->seconds, 60);
    }

    apply_offset("00 00:00:30.5");
    {
        const auto offset = tracked_sources.source_timestamp_offset(0);
        ASSERT_TRUE(offset.has_value());
        EXPECT_EQ(offset->seconds, 90);
        EXPECT_EQ(offset->nanosecond, 500000000);
    }

    remove_temp_export_file(log_path);
}

// NOTE: the align-time in-view selection test was removed with LogView1. Time
// alignment is not yet ported to LogView2; re-add a test once it is. See
// docs/logview1-vs-logview2.md.

} // namespace slayerlog
