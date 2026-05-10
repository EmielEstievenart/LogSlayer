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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

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
    LogEntry entry {"alpha.log", "INFO 2026-04-01 10:00:00 hello", test_timestamp()};
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
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

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
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(tracked_sources.open_source(parse_log_source(log_path.string())).has_value());
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen, Notifier(sink)});

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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    std::mutex model_mutex;
    std::vector<std::thread> background_tasks;
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen, Notifier(sink), &model_mutex, &background_tasks});

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
    bool saw_file_opened = false;
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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(tracked_sources.open_source(make_local_folder_source(folder_path.string())).has_value());
    std::mutex model_mutex;
    std::vector<std::thread> background_tasks;
    auto sink = std::make_shared<RecordingNotificationSink>();
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen, Notifier(sink), &model_mutex, &background_tasks});

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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

    const auto result = command_manager.execute("delete-filters");

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.close_palette_on_success);
    EXPECT_EQ(result.message, "Mark filters to delete and press Enter");
    ASSERT_NE(command_manager.active_command(), nullptr);

    ASSERT_TRUE(command_manager.active_command()->handle_event(ftxui::Event::Character(" ")).handled);
    ASSERT_TRUE(command_manager.active_command()->handle_event(ftxui::Event::ArrowDown).handled);
    ASSERT_TRUE(command_manager.active_command()->handle_event(ftxui::Event::ArrowDown).handled);
    ASSERT_TRUE(command_manager.active_command()->handle_event(ftxui::Event::Character(" ")).handled);
    const auto delete_result = command_manager.active_command()->handle_event(ftxui::Event::Return);

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
    LogController controller;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

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
    LogController controller;
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(tracked_sources.open_source(parse_log_source(log_path.string())).has_value());
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

    const auto result = command_manager.execute("set-time-format");

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.close_palette_on_success);
    EXPECT_EQ(result.message, "Select a source to configure");
    ASSERT_NE(command_manager.active_command(), nullptr);

    const auto source_result = command_manager.active_command()->handle_event(ftxui::Event::Return);
    ASSERT_TRUE(source_result.result.has_value());
    EXPECT_TRUE(source_result.result->success);
    EXPECT_FALSE(source_result.result->close_palette_on_success);
    EXPECT_NE(source_result.result->message.find("Select timestamp format for "), std::string::npos);

    remove_temp_export_file(log_path);
}

TEST(CommandRegistrarTest, AlignTimeCommandSetsSourceOffsetFromOriginalTimestamp)
{
    const auto alpha_log = make_temp_export_path();
    const auto beta_log  = make_temp_export_path();
    {
        std::ofstream output(alpha_log, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "2026-04-01T10:00:00 alpha first\n";
    }
    {
        std::ofstream output(beta_log, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "2026-04-01T10:05:00 beta second\n";
    }

    AllProcessedSources processed_sources;
    LogController controller;
    controller.text_view_controller().update_viewport_line_count(10);
    CommandManager command_manager;
    CommandPaletteModel command_palette_model;
    CommandPaletteController command_palette_controller(command_palette_model, command_manager);
    std::string header_text;
    auto screen = ftxui::ScreenInteractive::FixedSize(80, 24);
    AllTrackedSources tracked_sources;
    ASSERT_FALSE(tracked_sources.open_source(parse_log_source(alpha_log.string())).has_value());
    ASSERT_FALSE(tracked_sources.open_source(parse_log_source(beta_log.string())).has_value());
    ASSERT_FALSE(tracked_sources.set_source_timestamp_offset(0, *parse_log_timestamp_offset("00 00:01:00")).has_value());
    reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
    controller.text_view_controller().scroll_to_top();
    register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen});

    const auto result = command_manager.execute("align-time");
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(command_manager.active_command());
    ASSERT_TRUE(controller.time_alignment_active());

    ASSERT_TRUE(controller.handle_event(processed_sources, ftxui::Event::Return, {}).handled);
    ASSERT_TRUE(controller.handle_event(processed_sources, ftxui::Event::ArrowDown, {}).handled);
    ASSERT_TRUE(controller.handle_event(processed_sources, ftxui::Event::Return, {}).handled);

    EXPECT_FALSE(controller.time_alignment_active());
    const LogEntry* alpha_entry = nullptr;
    for (int index = 0; index < tracked_sources.line_count(); ++index)
    {
        const auto& entry = *tracked_sources.all_lines()[AllLineIndex {index}];
        if (entry.text.find("alpha first") != std::string::npos)
        {
            alpha_entry = &entry;
            break;
        }
    }

    ASSERT_NE(alpha_entry, nullptr);
    ASSERT_TRUE(alpha_entry->metadata.timestamp.has_value());
    ASSERT_TRUE(alpha_entry->metadata.offset_timestamp.has_value());
    EXPECT_EQ(format_log_timestamp_utc(*alpha_entry->metadata.timestamp), "2026-04-01 10:00:00");
    EXPECT_EQ(format_log_timestamp_utc(*alpha_entry->metadata.offset_timestamp), "2026-04-01 10:05:00");

    remove_temp_export_file(alpha_log);
    remove_temp_export_file(beta_log);
}

} // namespace slayerlog
