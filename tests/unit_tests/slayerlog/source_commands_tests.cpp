#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "command_manager.hpp"
#include "command_registrar.hpp"
#include "log_view_find_manager.hpp"
#include "log_view_service.hpp"
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

std::filesystem::path make_temp_log_path(const std::string& tag)
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_source_cmd_" + tag + "_" + unique_suffix + ".log");
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

struct SourceCommandFixture
{
    SourceCommandFixture() { register_log_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, find_manager); }

    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager {nullptr};
};

} // namespace

TEST(SourceCommandsTest, SetOffsetAppliesAnAbsoluteOffset)
{
    const auto log_path = make_temp_log_path("offset");
    write_timestamped_log(log_path);

    SourceCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_path.string()).success);

    const auto result = fixture.command_manager.execute("set-offset " + log_path.string() + " +00 00:01:30.5");
    ASSERT_TRUE(result.success) << result.message;

    auto offset = fixture.tracked_sources.source_timestamp_offset(0);
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, 90);
    EXPECT_EQ(offset->nanosecond, 500000000);

    // set-offset replaces (it does not accumulate like adjust-time-offset).
    ASSERT_TRUE(fixture.command_manager.execute("set-offset " + log_path.string() + " -00 00:00:10").success);
    offset = fixture.tracked_sources.source_timestamp_offset(0);
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, -10);
    EXPECT_EQ(offset->nanosecond, 0);

    const auto clear_result = fixture.command_manager.execute("clear-offset " + log_path.string());
    ASSERT_TRUE(clear_result.success) << clear_result.message;
    EXPECT_FALSE(fixture.tracked_sources.source_timestamp_offset(0).has_value());

    remove_file(log_path);
}

TEST(SourceCommandsTest, SetOffsetValidatesItsArguments)
{
    const auto log_path = make_temp_log_path("offset_errors");
    write_timestamped_log(log_path);

    SourceCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_path.string()).success);

    EXPECT_FALSE(fixture.command_manager.execute("set-offset").success);
    EXPECT_FALSE(fixture.command_manager.execute("set-offset " + log_path.string()).success);
    EXPECT_FALSE(fixture.command_manager.execute("set-offset " + log_path.string() + " not-an-offset").success);

    const auto unknown_result = fixture.command_manager.execute("set-offset no-such.log +00 00:00:01");
    EXPECT_FALSE(unknown_result.success);
    EXPECT_NE(unknown_result.message.find("Unknown source"), std::string::npos);

    remove_file(log_path);
}

TEST(SourceCommandsTest, SetTimeFormatTextualFormPinsAndResets)
{
    const auto log_path = make_temp_log_path("format");
    write_timestamped_log(log_path);

    SourceCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_path.string()).success);

    const auto set_result = fixture.command_manager.execute("set-time-format " + log_path.string() + " YYYY-MM-DDThh:mm:ss");
    ASSERT_TRUE(set_result.success) << set_result.message;
    EXPECT_EQ(fixture.command_manager.active_command(), nullptr);
    EXPECT_EQ(fixture.tracked_sources.source_timestamp_format_override(0), std::optional<std::string>("YYYY-MM-DDThh:mm:ss"));

    // A lone 'Y' fails the format compiler ("'Y' must appear as YY or YYYY").
    const auto invalid_result = fixture.command_manager.execute("set-time-format " + log_path.string() + " Y");
    EXPECT_FALSE(invalid_result.success);
    EXPECT_NE(invalid_result.message.find("Invalid timestamp format"), std::string::npos);

    const auto auto_result = fixture.command_manager.execute("set-time-format " + log_path.string() + " auto");
    ASSERT_TRUE(auto_result.success) << auto_result.message;
    EXPECT_FALSE(fixture.tracked_sources.source_timestamp_format_override(0).has_value());

    ASSERT_TRUE(fixture.command_manager.execute("set-time-format " + log_path.string() + " YYYY-MM-DDThh:mm:ss").success);
    const auto reset_result = fixture.command_manager.execute("reset-time-format " + log_path.string());
    ASSERT_TRUE(reset_result.success) << reset_result.message;
    EXPECT_FALSE(fixture.tracked_sources.source_timestamp_format_override(0).has_value());

    remove_file(log_path);
}

TEST(SourceCommandsTest, CloseCloseAllAndResetSession)
{
    const auto log_a = make_temp_log_path("close_a");
    const auto log_b = make_temp_log_path("close_b");
    write_timestamped_log(log_a);
    write_timestamped_log(log_b);

    SourceCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_a.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_b.string()).success);

    const auto close_result = fixture.command_manager.execute("close " + log_a.string());
    ASSERT_TRUE(close_result.success) << close_result.message;
    EXPECT_EQ(fixture.tracked_sources.source_count(), 1U);

    const auto close_all_result = fixture.command_manager.execute("close-all");
    ASSERT_TRUE(close_all_result.success) << close_all_result.message;
    EXPECT_EQ(fixture.tracked_sources.source_count(), 0U);
    EXPECT_FALSE(fixture.command_manager.execute("close-all").success);

    ASSERT_TRUE(fixture.command_manager.execute("open " + log_a.string()).success);
    ASSERT_TRUE(fixture.command_manager.execute("filter-in message").success);
    ASSERT_TRUE(fixture.command_manager.execute("hide-columns 3-5").success);

    const auto reset_result = fixture.command_manager.execute("reset-session");
    ASSERT_TRUE(reset_result.success) << reset_result.message;
    EXPECT_EQ(fixture.tracked_sources.source_count(), 0U);
    EXPECT_TRUE(fixture.processed_sources.include_filters().empty());
    EXPECT_FALSE(fixture.processed_sources.hidden_columns().has_value());
    EXPECT_EQ(fixture.processed_sources.line_count(), 0);

    remove_file(log_a);
    remove_file(log_b);
}

TEST(SourceCommandsTest, SourcesResolveByMnemonic)
{
    const auto log_path = make_temp_log_path("mnemonic");
    write_timestamped_log(log_path);

    SourceCommandFixture fixture;
    ASSERT_TRUE(fixture.command_manager.execute("open " + log_path.string()).success);

    const auto mnemonics = fixture.tracked_sources.source_mnemonics();
    ASSERT_EQ(mnemonics.size(), 1U);
    ASSERT_FALSE(mnemonics[0].empty());

    const auto result = fixture.command_manager.execute("set-offset " + mnemonics[0] + " +00 00:00:05");
    ASSERT_TRUE(result.success) << result.message;
    const auto offset = fixture.tracked_sources.source_timestamp_offset(0);
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, 5);

    remove_file(log_path);
}

} // namespace slayerlog
