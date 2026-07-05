#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "command_manager.hpp"
#include "command_registrar.hpp"
#include "commands/session_replay.hpp"
#include "log_view_find_manager.hpp"
#include "log_view_service.hpp"
#include "session_snapshot.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace slayerlog
{

namespace
{

class StubLogViewService : public LogViewService
{
public:
    StubLogViewService() = default;
    StubLogViewService(int first_visible_line, int viewport_line_count) : _first_visible_line(first_visible_line), _viewport_line_count(viewport_line_count) { }

    void rebuild_view(const AllProcessedSources&) override { }
    void reload(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources) override { processed_sources.rebuild_from_sources(tracked_sources); }
    bool go_to_line(const AllProcessedSources& processed_sources, int line_number) override { return processed_sources.visible_line_index_for_line_number(line_number).has_value(); }
    int first_visible_line() const override { return _first_visible_line; }
    int viewport_line_count() const override { return _viewport_line_count; }

private:
    int _first_visible_line  = 0;
    int _viewport_line_count = 0;
};

std::filesystem::path make_temp_log_path(const std::string& tag)
{
    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("slayerlog_snapshot_" + tag + "_" + unique_suffix + ".log");
}

void write_timestamped_log(const std::filesystem::path& path, unsigned start_second, int line_count)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    for (int line_index = 0; line_index < line_count; ++line_index)
    {
        output << "2026-04-01T10:00:0" << (start_second + static_cast<unsigned>(line_index)) % 10 << " message " << line_index << "\n";
    }
}

void remove_file(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove(path, error_code);
}

std::string absolute_reference(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal().string();
}

} // namespace

TEST(SessionSnapshotTest, OffsetSerializationRoundTrips)
{
    const std::vector<LogTimestampOffset> offsets {
        {0, 0}, {90, 500000000}, {-90, -500000000}, {3 * 86400 + 3661, 0}, {-42, 0}, {0, 250000000},
    };

    for (const auto& offset : offsets)
    {
        const std::string serialized = serialize_log_timestamp_offset(offset);
        const auto parsed            = parse_log_timestamp_offset(serialized);
        ASSERT_TRUE(parsed.has_value()) << serialized;
        EXPECT_EQ(parsed->seconds, offset.seconds) << serialized;
        EXPECT_EQ(parsed->nanosecond, offset.nanosecond) << serialized;
    }
}

TEST(SessionSnapshotTest, SerializesSourcesFormatsOffsetsAndToggles)
{
    const auto log_a = make_temp_log_path("a");
    const auto log_b = make_temp_log_path("b");
    write_timestamped_log(log_a, 0, 2);
    write_timestamped_log(log_b, 2, 2);

    AllTrackedSources tracked_sources;
    AllProcessedSources processed_sources;
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_a.string())).has_value());
    ASSERT_FALSE(open_source(tracked_sources, parse_log_source(log_b.string())).has_value());
    processed_sources.rebuild_from_sources(tracked_sources);

    ASSERT_FALSE(tracked_sources.set_source_timestamp_format(1, "YYYY-MM-DDThh:mm:ss").has_value());
    ASSERT_FALSE(tracked_sources.set_source_timestamp_offset(1, LogTimestampOffset {90, 500000000}).has_value());

    const auto commands = serialize_session_commands(tracked_sources, processed_sources, nullptr);

    const std::string reference_a = absolute_reference(log_a);
    const std::string reference_b = absolute_reference(log_b);
    const std::vector<std::string> expected {
        "open " + reference_a, "open " + reference_b, "set-time-format " + reference_b + " YYYY-MM-DDThh:mm:ss", "set-offset " + reference_b + " +00 00:01:30.5", "hide-identical-lines", "hide-original-time",
    };
    EXPECT_EQ(commands, expected);

    remove_file(log_a);
    remove_file(log_b);
}

TEST(SessionSnapshotTest, SerializesFiltersColumnsAndHideBefore)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines({
        LogEntry {"alpha.log", "keep one"},
        LogEntry {"alpha.log", "keep two"},
        LogEntry {"alpha.log", "keep three"},
    });
    processed_sources.add_include_filter("keep");
    processed_sources.add_exclude_filter("re:zzz");
    processed_sources.hide_columns(3, 7);
    processed_sources.hide_before_line_number(2);
    processed_sources.set_hide_identical_lines(false);
    processed_sources.set_show_original_time(true);

    AllTrackedSources tracked_sources;
    const auto commands = serialize_session_commands(tracked_sources, processed_sources, nullptr);

    const std::vector<std::string> expected {
        "filter-in keep", "filter-out re:zzz", "hide-columns 3-7", "show-identical-lines", "show-original-time", "hide-before-line 2",
    };
    EXPECT_EQ(commands, expected);
}

TEST(SessionSnapshotTest, EmitsGoToLineAtTheViewportCenter)
{
    AllProcessedSources processed_sources;
    std::vector<LogEntry> entries;
    for (int line_index = 0; line_index < 20; ++line_index)
    {
        entries.push_back(LogEntry {"alpha.log", "message " + std::to_string(line_index)});
    }
    processed_sources.append_lines(entries);

    AllTrackedSources tracked_sources;
    StubLogViewService log_view(4, 4);
    const auto commands = serialize_session_commands(tracked_sources, processed_sources, &log_view);

    // First visible row 4 plus half the 4-row viewport is row 6, whose 1-based
    // line number is 7.
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.back(), "go-to-line 7");
}

TEST(SessionSnapshotTest, SkipsGoToLineWithoutAViewport)
{
    AllProcessedSources processed_sources;
    processed_sources.append_lines({LogEntry {"alpha.log", "message"}});

    AllTrackedSources tracked_sources;
    StubLogViewService log_view;
    const auto commands = serialize_session_commands(tracked_sources, processed_sources, &log_view);

    for (const auto& command : commands)
    {
        EXPECT_EQ(command.rfind("go-to-line", 0), std::string::npos) << command;
    }
}

TEST(SessionSnapshotTest, SnapshotReplaysIntoAnIdenticalSnapshot)
{
    const auto log_a = make_temp_log_path("rt_a");
    const auto log_b = make_temp_log_path("rt_b");
    write_timestamped_log(log_a, 0, 3);
    write_timestamped_log(log_b, 3, 3);

    // Session A: built through the same commands a user would run.
    AllProcessedSources processed_a;
    AllTrackedSources tracked_a;
    StubLogViewService view_a;
    CommandManager manager_a;
    LogViewFindManager find_a(nullptr);
    register_log_view_commands(manager_a, {processed_a, view_a, tracked_a}, find_a);

    ASSERT_TRUE(manager_a.execute("open " + log_a.string()).success);
    ASSERT_TRUE(manager_a.execute("open " + log_b.string()).success);
    ASSERT_TRUE(manager_a.execute("set-time-format " + log_b.string() + " YYYY-MM-DDThh:mm:ss").success);
    ASSERT_TRUE(manager_a.execute("set-offset " + log_b.string() + " +00 00:01:30.5").success);
    ASSERT_TRUE(manager_a.execute("filter-in message").success);
    ASSERT_TRUE(manager_a.execute("filter-out re:zzz").success);
    ASSERT_TRUE(manager_a.execute("hide-columns 3-5").success);
    ASSERT_TRUE(manager_a.execute("show-identical-lines").success);
    ASSERT_TRUE(manager_a.execute("hide-before-line 2").success);

    const auto snapshot_a = serialize_session_commands(tracked_a, processed_a, nullptr);

    // Session B: a fresh model that only sees the snapshot.
    AllProcessedSources processed_b;
    AllTrackedSources tracked_b;
    StubLogViewService view_b;
    CommandManager manager_b;
    LogViewFindManager find_b(nullptr);
    const CommandContext context_b {processed_b, view_b, tracked_b};
    register_log_view_commands(manager_b, context_b, find_b);

    const auto report = replay_session_commands(snapshot_a, manager_b, context_b);
    EXPECT_TRUE(report.errors.empty()) << (report.errors.empty() ? "" : report.errors.front());
    EXPECT_EQ(report.executed_count, snapshot_a.size());

    const auto snapshot_b = serialize_session_commands(tracked_b, processed_b, nullptr);
    EXPECT_EQ(snapshot_a, snapshot_b);

    // And the replayed model matches the original beyond the snapshot itself.
    EXPECT_EQ(processed_b.line_count(), processed_a.line_count());
    EXPECT_EQ(tracked_b.source_count(), tracked_a.source_count());
    const auto offset_a = tracked_a.source_timestamp_offset(1);
    const auto offset_b = tracked_b.source_timestamp_offset(1);
    ASSERT_TRUE(offset_a.has_value());
    ASSERT_TRUE(offset_b.has_value());
    EXPECT_EQ(offset_b->seconds, offset_a->seconds);
    EXPECT_EQ(offset_b->nanosecond, offset_a->nanosecond);

    remove_file(log_a);
    remove_file(log_b);
}

} // namespace slayerlog
