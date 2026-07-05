#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "command_manager.hpp"
#include "command_registrar.hpp"
#include "commands/session_replay.hpp"
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
    return std::filesystem::temp_directory_path() / ("slayerlog_replay_" + tag + "_" + unique_suffix + ".log");
}

void remove_file(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove(path, error_code);
}

} // namespace

TEST(SessionReplayTest, OpensRunSynchronouslyEvenWithABackgroundCapableContext)
{
    const auto log_path = make_temp_log_path("sync");
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        output << "plain line\n";
    }

    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager(nullptr);
    std::mutex model_mutex;
    std::vector<std::thread> background_tasks;
    const CommandContext context {processed_sources, log_view, tracked_sources, {}, &model_mutex, &background_tasks};
    register_log_view_commands(command_manager, context, find_manager);

    // The follow-up command relies on the source existing, which is exactly why
    // replay must not defer the open to a background task.
    const auto report = replay_session_commands({"open " + log_path.string(), "set-offset " + log_path.string() + " +00 00:00:05"}, command_manager, context);

    EXPECT_TRUE(report.errors.empty()) << (report.errors.empty() ? "" : report.errors.front());
    EXPECT_TRUE(background_tasks.empty());
    ASSERT_EQ(tracked_sources.source_count(), 1U);
    const auto offset = tracked_sources.source_timestamp_offset(0);
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(offset->seconds, 5);

    remove_file(log_path);
}

TEST(SessionReplayTest, CollectsErrorsAndKeepsGoing)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines({LogEntry {"alpha.log", "keep this"}});

    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager(nullptr);
    const CommandContext context {processed_sources, log_view, tracked_sources};
    register_log_view_commands(command_manager, context, find_manager);

    const auto report = replay_session_commands({"open C:\\does\\not\\exist.log", "unknown-command", "filter-in keep", ""}, command_manager, context);

    EXPECT_EQ(report.executed_count, 3U);
    ASSERT_EQ(report.errors.size(), 2U);
    EXPECT_NE(report.errors[0].find("open C:\\does\\not\\exist.log"), std::string::npos);
    EXPECT_NE(report.errors[1].find("unknown-command"), std::string::npos);
    ASSERT_EQ(processed_sources.include_filters().size(), 1U);
    EXPECT_EQ(processed_sources.include_filters()[0], "keep");
}

TEST(SessionReplayTest, InteractiveCommandsAreCancelledAndReported)
{
    const auto log_path = make_temp_log_path("interactive");
    {
        std::ofstream output(log_path, std::ios::binary | std::ios::trunc);
        output << "2026-04-01T10:00:00 line\n";
    }

    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    CommandManager command_manager;
    LogViewFindManager find_manager(nullptr);
    const CommandContext context {processed_sources, log_view, tracked_sources};
    register_log_view_commands(command_manager, context, find_manager);

    const auto report = replay_session_commands({"open " + log_path.string(), "adjust-time-offset"}, command_manager, context);

    ASSERT_EQ(report.errors.size(), 1U);
    EXPECT_NE(report.errors[0].find("adjust-time-offset"), std::string::npos);
    EXPECT_NE(report.errors[0].find("interactive"), std::string::npos);
    EXPECT_EQ(command_manager.active_command(), nullptr);

    remove_file(log_path);
}

} // namespace slayerlog
