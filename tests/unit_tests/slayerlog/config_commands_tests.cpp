#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "command_manager.hpp"
#include "command_registrar.hpp"
#include "commands/session_config_store.hpp"
#include "implementations/export_config_command.hpp"
#include "log_view_find_manager.hpp"
#include "log_view_service.hpp"
#include "session_snapshot.hpp"
#include "settings_store.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

class StubLogViewService : public LogViewService
{
public:
    void rebuild_view(const AllProcessedSources&) override { }
    void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) override { processed_sources.rebuild_from_sources(tracked_sources); }
    bool go_to_line(const AllProcessedSources&, int) override { return false; }
    int first_visible_line() const override { return 0; }
    int viewport_line_count() const override { return 0; }
};

std::filesystem::path make_temp_path(const std::string& tag, const std::string& extension)
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_config_cmd_" + tag + "_" + unique_suffix + extension);
}

void write_timestamped_log(const std::filesystem::path& path)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "2026-04-01T10:00:00 message one\n";
    output << "2026-04-01T10:00:01 message two\n";
}

void remove_file(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove(path, error_code);
}

std::string read_file_contents(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::filesystem::path make_temp_settings_path()
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory     = std::filesystem::temp_directory_path() / ("slayerlog_config_cmd_" + unique_suffix);
    std::filesystem::create_directories(directory);
    return directory / "settings.ini";
}

/// Everything a config-command test needs, wired like main() does it. The
/// settings ini lives in its own directory so the .bak files saves create are
/// cleaned up with it.
struct ConfigCommandFixture
{
    ConfigCommandFixture() : settings_path(make_temp_settings_path()), settings_store(settings_path)
    {
        std::string error_message;
        EXPECT_TRUE(settings_store.load(error_message));
        register_log_view_commands(command_manager, {processed_sources, log_view, tracked_sources, {}, nullptr, nullptr, settings_path, &settings_store}, find_manager);
    }

    ~ConfigCommandFixture()
    {
        std::error_code error_code;
        std::filesystem::remove_all(settings_path.parent_path(), error_code);
    }

    std::filesystem::path settings_path;
    SettingsStore settings_store;
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager {nullptr};
};

} // namespace

TEST(ConfigCommandsTest, SaveThenLoadRestoresTheSavedSession)
{
    const auto log_a = make_temp_path("a", ".log");
    const auto log_b = make_temp_path("b", ".log");
    write_timestamped_log(log_a);
    write_timestamped_log(log_b);

    ConfigCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_a.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_b.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("set-offset " + log_b.string() + " +00 00:00:30").success);
    ASSERT_TRUE(fixture.command_manager.execute("filter-in message").success);

    const auto saved_snapshot = serialize_session_commands(fixture.tracked_sources, fixture.processed_sources, nullptr);
    const auto save_result    = fixture.command_manager.execute("save-config crashhunt");
    ASSERT_TRUE(save_result.success) << save_result.message;

    // Wreck the session, then load the config back.
    ASSERT_TRUE(fixture.command_manager.execute("close " + log_b.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("reset-filters").success);
    ASSERT_TRUE(fixture.command_manager.execute("filter-out message").success);

    const auto load_result = fixture.command_manager.execute("load-config crashhunt");
    ASSERT_TRUE(load_result.success) << load_result.message;

    EXPECT_EQ(serialize_session_commands(fixture.tracked_sources, fixture.processed_sources, nullptr), saved_snapshot);
    EXPECT_EQ(fixture.tracked_sources.source_count(), 2U);
    ASSERT_EQ(fixture.processed_sources.include_filters().size(), 1U);
    EXPECT_TRUE(fixture.processed_sources.exclude_filters().empty());

    remove_file(log_a);
    remove_file(log_b);
}

TEST(ConfigCommandsTest, LoadConfigReplacesTheCurrentSession)
{
    const auto log_a = make_temp_path("replace", ".log");
    write_timestamped_log(log_a);

    ConfigCommandFixture fixture;
    // Save an empty-session config, open a file, then load: the file must close.
    ASSERT_TRUE(fixture.command_manager.execute("save-config empty").success);
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_a.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("filter-in message").success);

    const auto load_result = fixture.command_manager.execute("load-config empty");
    ASSERT_TRUE(load_result.success) << load_result.message;

    EXPECT_EQ(fixture.tracked_sources.source_count(), 0U);
    EXPECT_TRUE(fixture.processed_sources.include_filters().empty());
    EXPECT_EQ(fixture.processed_sources.line_count(), 0);

    remove_file(log_a);
}

TEST(ConfigCommandsTest, ListAndDeleteConfigs)
{
    ConfigCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("save-config alpha").success);
    ASSERT_TRUE(fixture.command_manager.execute("save-config beta").success);

    const auto list_result = fixture.command_manager.execute("list-configs");
    ASSERT_TRUE(list_result.success);
    EXPECT_EQ(list_result.message, "Saved configs: alpha, beta");

    const auto delete_result = fixture.command_manager.execute("delete-config alpha");
    ASSERT_TRUE(delete_result.success);

    const auto second_list = fixture.command_manager.execute("list-configs");
    EXPECT_EQ(second_list.message, "Saved configs: beta");

    const auto missing_delete = fixture.command_manager.execute("delete-config alpha");
    EXPECT_FALSE(missing_delete.success);
}

TEST(ConfigCommandsTest, LoadConfigReportsUnknownNames)
{
    ConfigCommandFixture fixture;

    const auto result = fixture.command_manager.execute("load-config nope");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("No config named 'nope'"), std::string::npos);
}

TEST(ConfigCommandsTest, LoadConfigSkipsInteractiveCommands)
{
    const auto log_a = make_temp_path("interactive", ".log");
    write_timestamped_log(log_a);

    ConfigCommandFixture fixture;
    ASSERT_FALSE(save_session_config(fixture.settings_store, "picker", {"open " + log_a.string(), "adjust-time-offset"}).has_value());

    const auto result = fixture.command_manager.execute("load-config picker");

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NE(result.message.find("1 errors"), std::string::npos);
    EXPECT_EQ(fixture.command_manager.active_command(), nullptr);
    EXPECT_EQ(fixture.tracked_sources.source_count(), 1U);

    remove_file(log_a);
}

TEST(ConfigCommandsTest, ConfigCommandsFailCleanlyWithoutASettingsStore)
{
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager(nullptr);
    register_log_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, find_manager);

    EXPECT_FALSE(command_manager.execute("save-config foo").success);
    EXPECT_FALSE(command_manager.execute("load-config foo").success);
    EXPECT_FALSE(command_manager.execute("list-configs").success);
    EXPECT_FALSE(command_manager.execute("delete-config foo").success);
}

TEST(ConfigCommandsTest, ExportConfigWritesARunnableBatchScript)
{
    const auto log_a = make_temp_path("export", ".log");
    write_timestamped_log(log_a);
    const auto script_path = make_temp_path("script", ".bat");

    ConfigCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_a.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("filter-in message").success);

    ExportConfigCommand export_command({fixture.processed_sources, fixture.log_view, fixture.tracked_sources}, [](const std::string&, std::string&) { return true; }, []() { return std::string("C:\\tools\\LogSlayer.exe"); });
    const auto result = export_command.execute(script_path.string());

    ASSERT_TRUE(result.success) << result.message;
    const std::string script = read_file_contents(script_path);
    EXPECT_NE(script.find("@echo off"), std::string::npos);
    EXPECT_NE(script.find("\"C:\\tools\\LogSlayer.exe\""), std::string::npos);
    EXPECT_NE(script.find("--cmd \"open "), std::string::npos);
    EXPECT_NE(script.find("--cmd \"filter-in message\""), std::string::npos);

    remove_file(script_path);
    remove_file(log_a);
}

TEST(ConfigCommandsTest, ExportConfigCopiesTheInvocationToTheClipboard)
{
    ConfigCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("filter-in message").success);

    std::string copied_text;
    ExportConfigCommand export_command(
        {fixture.processed_sources, fixture.log_view, fixture.tracked_sources},
        [&](const std::string& text, std::string&)
        {
            copied_text = text;
            return true;
        },
        []() { return std::string("LogSlayer"); });

    const auto result = export_command.execute("");

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NE(copied_text.find("LogSlayer"), std::string::npos);
    EXPECT_NE(copied_text.find("--cmd"), std::string::npos);
    EXPECT_NE(copied_text.find("filter-in message"), std::string::npos);
}

TEST(ConfigCommandsTest, ExportConfigRejectsUnknownExtensions)
{
    ConfigCommandFixture fixture;

    const auto result = fixture.command_manager.execute("export-config session.txt");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Unsupported script extension"), std::string::npos);
}

} // namespace slayerlog
