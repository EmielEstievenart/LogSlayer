#pragma once

#include <utility>
#include <vector>

#include "notifications/notification.hpp"

namespace slayerlog
{

/// Test double capturing everything posted through a Notifier, in order.
/// show() hands out sequential ids starting at 1.
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

} // namespace slayerlog
