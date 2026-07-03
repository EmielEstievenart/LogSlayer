#include "notification.hpp"

#include <utility>

namespace slayerlog
{

namespace
{

Notification make_level_notification(std::string title, std::string message, NotificationLevel level)
{
    Notification notification;
    notification.title   = std::move(title);
    notification.message = std::move(message);
    notification.level   = level;
    return notification;
}

bool is_completed_progress(const Notification& notification)
{
    return notification.dismiss_when_done && notification.progress.has_value() && *notification.progress >= 1.0F;
}

/// The dismiss_when_done policy: a completed progress notification that would
/// otherwise be sticky gets a finite timeout, so it fades out in every sink.
void apply_completion_policy(Notification& notification)
{
    if (is_completed_progress(notification) && notification.timeout <= std::chrono::milliseconds(0))
    {
        notification.timeout = completion_dismiss_timeout;
    }
}

} // namespace

Notification make_progress_notification(std::string title, std::string message, float progress)
{
    Notification notification = make_level_notification(std::move(title), std::move(message), NotificationLevel::Info);
    notification.progress     = progress;
    notification.timeout      = std::chrono::milliseconds(0);
    return notification;
}

Notifier::Notifier(std::shared_ptr<NotificationSink> sink) : _sink(std::move(sink))
{
}

NotificationId Notifier::show(Notification notification) const
{
    if (_sink == nullptr)
    {
        return 0;
    }

    apply_completion_policy(notification);
    return _sink->show(std::move(notification));
}

void Notifier::update(NotificationId id, Notification notification) const
{
    if (_sink == nullptr || id == 0)
    {
        return;
    }

    apply_completion_policy(notification);
    _sink->update(id, std::move(notification));
}

void Notifier::dismiss(NotificationId id) const
{
    if (_sink == nullptr || id == 0)
    {
        return;
    }

    _sink->dismiss(id);
}

void Notifier::info(std::string title, std::string message) const
{
    (void)show(make_level_notification(std::move(title), std::move(message), NotificationLevel::Info));
}

void Notifier::success(std::string title, std::string message) const
{
    (void)show(make_level_notification(std::move(title), std::move(message), NotificationLevel::Success));
}

void Notifier::warning(std::string title, std::string message) const
{
    (void)show(make_level_notification(std::move(title), std::move(message), NotificationLevel::Warning));
}

void Notifier::error(std::string title, std::string message) const
{
    Notification notification = make_level_notification(std::move(title), std::move(message), NotificationLevel::Error);
    notification.timeout      = error_notification_timeout;
    (void)show(std::move(notification));
}

NotificationHandle::NotificationHandle(Notifier notifier) : _notifier(std::move(notifier))
{
}

void NotificationHandle::show_or_update(Notification notification)
{
    // Whether this notification will expire on its own once delivered: a positive
    // timeout, or completed progress (which Notifier arms with a finite timeout).
    const bool expires = notification.timeout > std::chrono::milliseconds(0) || is_completed_progress(notification);

    if (_id == 0 || _last_sent_expires)
    {
        _id = _notifier.show(std::move(notification));
    }
    else
    {
        _notifier.update(_id, std::move(notification));
    }

    _last_sent_expires = expires;
}

void NotificationHandle::dismiss()
{
    _notifier.dismiss(_id);
    _id                = 0;
    _last_sent_expires = false;
}

NotificationId NotificationHandle::id() const
{
    return _id;
}

} // namespace slayerlog
