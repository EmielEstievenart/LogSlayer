#pragma once

#include <memory>

#include "notification.hpp"

class ToastHostComponent;

namespace slayerlog
{

class FtxuiToastNotificationSink : public NotificationSink
{
public:
    explicit FtxuiToastNotificationSink(std::shared_ptr<ToastHostComponent> toast_host);

    NotificationId show(Notification notification) override;
    void update(NotificationId id, Notification notification) override;
    void dismiss(NotificationId id) override;

private:
    // Weak on purpose: Notifier copies end up in long-lived model objects (tracked
    // sources, commands), and a strong reference here would let them keep the whole
    // FTXUI component tree alive past the model it renders. Notifications after the
    // toast host is gone become no-ops instead.
    std::weak_ptr<ToastHostComponent> _toast_host;
};

} // namespace slayerlog
