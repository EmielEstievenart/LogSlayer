#include "ftxui_toast_notification_sink.hpp"

#include <ftxui_components/toast_component.hpp>

#include <utility>

namespace slayerlog
{

namespace
{

ToastLevel to_toast_level(NotificationLevel level)
{
    switch (level)
    {
    case NotificationLevel::Success:
        return ToastLevel::Success;
    case NotificationLevel::Warning:
        return ToastLevel::Warning;
    case NotificationLevel::Error:
        return ToastLevel::Error;
    case NotificationLevel::Info:
    default:
        return ToastLevel::Info;
    }
}

ToastOption to_toast_option(Notification notification)
{
    ToastOption option;
    option.title             = std::move(notification.title);
    option.message           = std::move(notification.message);
    option.level             = to_toast_level(notification.level);
    option.progress          = notification.progress;
    option.timeout           = notification.timeout;
    option.dismiss_when_done = notification.dismiss_when_done;
    return option;
}

} // namespace

FtxuiToastNotificationSink::FtxuiToastNotificationSink(std::shared_ptr<ToastHostComponent> toast_host) : _toast_host(std::move(toast_host))
{
}

NotificationId FtxuiToastNotificationSink::show(Notification notification)
{
    const auto toast_host = _toast_host.lock();
    if (toast_host == nullptr)
    {
        return 0;
    }

    return toast_host->show(to_toast_option(std::move(notification)));
}

void FtxuiToastNotificationSink::update(NotificationId id, Notification notification)
{
    const auto toast_host = _toast_host.lock();
    if (toast_host == nullptr || id == 0)
    {
        return;
    }

    toast_host->update(id, to_toast_option(std::move(notification)));
}

void FtxuiToastNotificationSink::dismiss(NotificationId id)
{
    const auto toast_host = _toast_host.lock();
    if (toast_host == nullptr || id == 0)
    {
        return;
    }

    toast_host->dismiss(id);
}

} // namespace slayerlog
