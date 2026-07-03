#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>

#include "notifications/notification.hpp"

#include "recording_notification_sink.hpp"

namespace slayerlog
{

TEST(NotificationTest, DefaultNotifierDropsEverything)
{
    Notifier notifier;

    EXPECT_EQ(notifier.show({}), 0U);
    notifier.update(1, {});
    notifier.dismiss(1);
    notifier.info("title", "message");
    notifier.success("title", "message");
    notifier.warning("title", "message");
    notifier.error("title", "message");

    NotificationHandle handle(notifier);
    handle.show_or_update({});
    EXPECT_EQ(handle.id(), 0U);
    handle.dismiss();
}

TEST(NotificationTest, ConvenienceMethodsSetLevelAndTimeout)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    Notifier notifier(sink);

    notifier.info("Info title", "info message");
    notifier.success("Success title", "success message");
    notifier.warning("Warning title", "warning message");
    notifier.error("Error title", "error message");

    ASSERT_EQ(sink->notifications.size(), 4U);
    EXPECT_EQ(sink->notifications[0].title, "Info title");
    EXPECT_EQ(sink->notifications[0].message, "info message");
    EXPECT_EQ(sink->notifications[0].level, NotificationLevel::Info);
    EXPECT_EQ(sink->notifications[0].timeout, std::chrono::milliseconds(std::chrono::seconds(6)));
    EXPECT_EQ(sink->notifications[1].level, NotificationLevel::Success);
    EXPECT_EQ(sink->notifications[2].level, NotificationLevel::Warning);
    EXPECT_EQ(sink->notifications[3].level, NotificationLevel::Error);
    EXPECT_EQ(sink->notifications[3].timeout, std::chrono::milliseconds(error_notification_timeout));
}

TEST(NotificationTest, MakeProgressNotificationIsSticky)
{
    const Notification notification = make_progress_notification("Working", "half way", 0.5F);

    EXPECT_EQ(notification.title, "Working");
    EXPECT_EQ(notification.message, "half way");
    EXPECT_EQ(notification.level, NotificationLevel::Info);
    ASSERT_TRUE(notification.progress.has_value());
    EXPECT_FLOAT_EQ(*notification.progress, 0.5F);
    EXPECT_LE(notification.timeout.count(), 0);
    EXPECT_TRUE(notification.dismiss_when_done);
}

TEST(NotificationTest, NotifierArmsCompletionTimeoutOnFinishedStickyProgress)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    Notifier notifier(sink);

    const auto finished_id = notifier.show(make_progress_notification("Working", "done", 1.0F));
    EXPECT_EQ(finished_id, 1U);

    const auto unfinished_id = notifier.show(make_progress_notification("Working", "half way", 0.5F));
    EXPECT_EQ(unfinished_id, 2U);

    Notification explicit_timeout = make_progress_notification("Working", "done", 1.0F);
    explicit_timeout.timeout      = std::chrono::seconds(2);
    EXPECT_EQ(notifier.show(std::move(explicit_timeout)), 3U);

    Notification opted_out      = make_progress_notification("Working", "done", 1.0F);
    opted_out.dismiss_when_done = false;
    EXPECT_EQ(notifier.show(std::move(opted_out)), 4U);

    notifier.update(finished_id, make_progress_notification("Working", "done again", 1.0F));

    ASSERT_EQ(sink->notifications.size(), 5U);
    EXPECT_EQ(sink->notifications[0].timeout, std::chrono::milliseconds(completion_dismiss_timeout));
    EXPECT_LE(sink->notifications[1].timeout.count(), 0);
    EXPECT_EQ(sink->notifications[2].timeout, std::chrono::milliseconds(std::chrono::seconds(2)));
    EXPECT_LE(sink->notifications[3].timeout.count(), 0);
    EXPECT_EQ(sink->notifications[4].timeout, std::chrono::milliseconds(completion_dismiss_timeout));
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({finished_id}));
}

TEST(NotificationTest, HandleShowsOnceThenUpdatesInPlace)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    NotificationHandle handle {Notifier(sink)};

    handle.show_or_update(make_progress_notification("Working", "0%", 0.0F));
    EXPECT_EQ(handle.id(), 1U);
    handle.show_or_update(make_progress_notification("Working", "50%", 0.5F));
    handle.show_or_update(make_progress_notification("Working", "100%", 1.0F));

    EXPECT_EQ(handle.id(), 1U);
    ASSERT_EQ(sink->notifications.size(), 3U);
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({1, 1}));
}

TEST(NotificationTest, HandleStartsFreshNotificationAfterOneThatExpires)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    NotificationHandle handle {Notifier(sink)};

    // Completed progress auto-dismisses, so updating it later would be silently
    // dropped by the sink; the handle must show a new notification instead.
    handle.show_or_update(make_progress_notification("Working", "100%", 1.0F));
    EXPECT_EQ(handle.id(), 1U);

    handle.show_or_update(make_progress_notification("Working", "0%", 0.0F));
    EXPECT_EQ(handle.id(), 2U);
    EXPECT_TRUE(sink->updated_ids.empty());

    // A plain finite-timeout notification also expires on its own...
    Notification transient;
    transient.title = "Working";
    handle.show_or_update(std::move(transient));
    EXPECT_EQ(handle.id(), 2U);
    EXPECT_EQ(sink->updated_ids, std::vector<NotificationId>({2}));

    // ...so the next one starts fresh again.
    handle.show_or_update(make_progress_notification("Working", "0%", 0.0F));
    EXPECT_EQ(handle.id(), 3U);
    EXPECT_EQ(sink->notifications.size(), 4U);
}

TEST(NotificationTest, HandleDismissForwardsOnceAndResets)
{
    auto sink = std::make_shared<RecordingNotificationSink>();
    NotificationHandle handle {Notifier(sink)};

    handle.show_or_update(make_progress_notification("Working", "0%", 0.0F));
    handle.dismiss();

    EXPECT_EQ(handle.id(), 0U);
    EXPECT_EQ(sink->dismissed_ids, std::vector<NotificationId>({1}));

    handle.dismiss();
    EXPECT_EQ(sink->dismissed_ids.size(), 1U);

    handle.show_or_update(make_progress_notification("Working", "0%", 0.0F));
    EXPECT_EQ(handle.id(), 2U);
}

} // namespace slayerlog
