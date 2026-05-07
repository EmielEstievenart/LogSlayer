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
    std::shared_ptr<ToastHostComponent> _toast_host;
};

} // namespace slayerlog
