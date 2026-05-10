#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

TEST(BlfFileWatcherTest, FormattingImportedLinesReportsProgressThroughNotifier)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    BlfFileWatcher watcher("trace.blf", Notifier(sink),
                           [](const std::string&, const Notifier&)
                           {
                               return BlfFileWatcher::ImportResult {true,
                                                                     0,
                                                                     {"{\"kind\":\"can_frame\",\"ts\":\"2021-04-08T13:50:21.104969Z\",\"channel\":1,\"direction\":\"rx\",\"id\":\"0x0C7\",\"data_len\":8,\"dlc\":8,\"data\":\"0000000000000404\",\"is_fd\":false}",
                                                                      "{\"kind\":\"can_frame\",\"ts\":\"2021-04-08T13:50:21.105216Z\",\"channel\":1,\"direction\":\"rx\",\"id\":\"0x241\",\"data_len\":8,\"dlc\":8,\"data\":\"0120000000000524\",\"is_fd\":false}",
                                                                      "{\"kind\":\"can_frame\",\"ts\":\"2021-04-08T13:50:21.105450Z\",\"channel\":1,\"direction\":\"rx\",\"id\":\"0x103\",\"data_len\":8,\"dlc\":8,\"data\":\"8F478F000078E4DB\",\"is_fd\":false}"},
                                                                     {},
                                                                     {}};
                           });

    std::vector<std::string> lines;
    ASSERT_TRUE(watcher.poll(lines));

    ASSERT_EQ(sink->notifications.size(), 4U);
    EXPECT_EQ(sink->notifications[0].title, "Formatting BLF content");
    EXPECT_EQ(sink->notifications[0].message, "0% trace.blf");
    EXPECT_EQ(sink->notifications[1].message, "33% trace.blf");
    EXPECT_EQ(sink->notifications[2].message, "67% trace.blf");
    EXPECT_EQ(sink->notifications[3].message, "100% trace.blf");
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({1, 1, 1}));
}

} // namespace slayerlog
