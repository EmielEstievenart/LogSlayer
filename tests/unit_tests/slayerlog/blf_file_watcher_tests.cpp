#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "watchers/blf_file_watcher.hpp"

namespace slayerlog
{

namespace
{

void expect_poll_lines(BlfFileWatcher& watcher, const std::vector<std::string>& expected_lines)
{
    std::vector<std::string> lines {"stale data"};
    ASSERT_TRUE(watcher.poll(lines));
    EXPECT_EQ(lines, expected_lines);
}

void expect_no_poll_lines(BlfFileWatcher& watcher)
{
    std::vector<std::string> lines {"stale data"};
    EXPECT_FALSE(watcher.poll(lines));
    EXPECT_TRUE(lines.empty());
}

} // namespace

TEST(BlfFileWatcherTest, FirstPollReturnsImporterStdoutLines)
{
    BlfFileWatcher watcher("trace.blf", {},
                           [](const std::string& file_path, const Notifier&)
                           {
                               EXPECT_EQ(file_path, "trace.blf");
                               return BlfFileWatcher::ImportResult {true, 0, {"{\"kind\":\"can_frame\",\"ts\":\"2021-04-08T13:50:21.104969Z\",\"channel\":1,\"direction\":\"rx\",\"id\":\"0x0C7\",\"data_len\":8,\"dlc\":8,\"data\":\"0000000000000404\",\"is_fd\":false}"}, {}, {}};
                           });

    expect_poll_lines(watcher, {"2021-04-08T13:50:21.104969 CAN1 RX 0x0C7 [8] 00 00 00 00 00 00 04 04"});
}

TEST(BlfFileWatcherTest, SecondPollReturnsNothing)
{
    int call_count = 0;
    BlfFileWatcher watcher("trace.blf", {},
                           [&call_count](const std::string&, const Notifier&)
                           {
                               ++call_count;
                               return BlfFileWatcher::ImportResult {true, 0, {"{\"kind\":\"blf_object\"}"}, {}, {}};
                           });

    expect_poll_lines(watcher, {"{\"kind\":\"blf_object\"}"});
    expect_no_poll_lines(watcher);
    EXPECT_EQ(call_count, 1);
}

TEST(BlfFileWatcherTest, NonzeroImporterWithStdoutAppendsErrorLine)
{
    BlfFileWatcher watcher("trace.blf", {},
                           [](const std::string&, const Notifier&)
                           {
                               return BlfFileWatcher::ImportResult {true, 7, {"{\"kind\":\"import_error\",\"message\":\"from script\"}"}, "stderr details\n", {}};
                           });

    std::vector<std::string> lines;
    ASSERT_TRUE(watcher.poll(lines));
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "{\"kind\":\"import_error\",\"message\":\"from script\"}");
    EXPECT_NE(lines[1].find("\"kind\":\"import_error\""), std::string::npos);
    EXPECT_NE(lines[1].find("BLF importer exited with code 7"), std::string::npos);
    EXPECT_NE(lines[1].find("stderr details"), std::string::npos);
}

TEST(BlfFileWatcherTest, NonzeroImporterWithoutStdoutReturnsErrorLine)
{
    BlfFileWatcher watcher("trace.blf", {},
                           [](const std::string&, const Notifier&)
                           {
                               return BlfFileWatcher::ImportResult {true, 2, {}, {}, {}};
                           });

    std::vector<std::string> lines;
    ASSERT_TRUE(watcher.poll(lines));
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find("\"kind\":\"import_error\""), std::string::npos);
    EXPECT_NE(lines[0].find("BLF importer exited with code 2"), std::string::npos);
}

TEST(BlfFileWatcherTest, StartFailureReturnsErrorLine)
{
    BlfFileWatcher watcher("trace.blf", {},
                           [](const std::string&, const Notifier&)
                           {
                               return BlfFileWatcher::ImportResult {false, 0, {}, {}, "missing python"};
                           });

    std::vector<std::string> lines;
    ASSERT_TRUE(watcher.poll(lines));
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find("\"kind\":\"import_error\""), std::string::npos);
    EXPECT_NE(lines[0].find("missing python"), std::string::npos);
}

TEST(BlfFileWatcherTest, RunnerExceptionReturnsErrorLine)
{
    BlfFileWatcher watcher("trace.blf", {},
                           [](const std::string&, const Notifier&) -> BlfFileWatcher::ImportResult
                           {
                               throw std::runtime_error("runner exploded");
                           });

    std::vector<std::string> lines;
    ASSERT_TRUE(watcher.poll(lines));
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_NE(lines[0].find("\"kind\":\"import_error\""), std::string::npos);
    EXPECT_NE(lines[0].find("runner exploded"), std::string::npos);
}

} // namespace slayerlog
