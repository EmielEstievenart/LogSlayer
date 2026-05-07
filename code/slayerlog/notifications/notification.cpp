#include "notification.hpp"

#include <utility>

namespace slayerlog
{

Notifier::Notifier(std::shared_ptr<NotificationSink> sink) : _sink(std::move(sink))
{
}

NotificationId Notifier::show(Notification notification) const
{
    if (_sink == nullptr)
    {
        return 0;
    }

    return _sink->show(std::move(notification));
}

void Notifier::update(NotificationId id, Notification notification) const
{
    if (_sink == nullptr || id == 0)
    {
        return;
    }

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

NotificationHandle::NotificationHandle(Notifier notifier) : _notifier(std::move(notifier))
{
}

NotificationId NotificationHandle::show_or_update(Notification notification)
{
    if (_id == 0)
    {
        _id = _notifier.show(std::move(notification));
        return _id;
    }

    _notifier.update(_id, std::move(notification));
    return _id;
}

void NotificationHandle::dismiss()
{
    _notifier.dismiss(_id);
    _id = 0;
}

NotificationId NotificationHandle::id() const
{
    return _id;
}

} // namespace slayerlog
