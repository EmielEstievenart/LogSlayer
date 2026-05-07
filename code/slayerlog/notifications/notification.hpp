#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace slayerlog
{

enum class NotificationLevel
{
    Info,
    Success,
    Warning,
    Error,
};

using NotificationId = std::uint64_t;

struct Notification
{
    std::string title;
    std::string message;
    NotificationLevel level = NotificationLevel::Info;
    std::optional<float> progress;
    std::chrono::milliseconds timeout = std::chrono::seconds(3);
    bool dismiss_when_done = true;
};

class NotificationSink
{
public:
    virtual ~NotificationSink() = default;

    virtual NotificationId show(Notification notification) = 0;
    virtual void update(NotificationId id, Notification notification) = 0;
    virtual void dismiss(NotificationId id) = 0;
};

class Notifier
{
public:
    Notifier() = default;
    explicit Notifier(std::shared_ptr<NotificationSink> sink);

    [[nodiscard]] NotificationId show(Notification notification) const;
    void update(NotificationId id, Notification notification) const;
    void dismiss(NotificationId id) const;

private:
    std::shared_ptr<NotificationSink> _sink;
};

class NotificationHandle
{
public:
    NotificationHandle() = default;
    explicit NotificationHandle(Notifier notifier);

    [[nodiscard]] NotificationId show_or_update(Notification notification);
    void dismiss();
    [[nodiscard]] NotificationId id() const;

private:
    Notifier _notifier;
    NotificationId _id = 0;
};

} // namespace slayerlog
