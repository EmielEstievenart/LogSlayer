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

/// Identifies one live notification within its sink. 0 is the reserved "no
/// notification" value: show() returns it when no sink (or no UI) is attached,
/// and update()/dismiss() ignore it.
using NotificationId = std::uint64_t;

/// How long a completed progress notification (progress >= 1 with a sticky
/// timeout) stays visible before auto-dismissing. Applied by Notifier so every
/// sink behaves the same; see Notification::dismiss_when_done.
inline constexpr std::chrono::seconds completion_dismiss_timeout {4};

/// How long notifications shown via Notifier::error stay visible.
inline constexpr std::chrono::seconds error_notification_timeout {10};

/// One user-facing notification (rendered as a toast by the terminal UI).
///
/// Timeout semantics: a positive timeout auto-dismisses the notification after
/// that duration; a timeout <= 0 makes it sticky (visible until updated to
/// something that expires, or dismissed). Keep in-progress notifications sticky:
/// sinks silently ignore updates to an expired id, so a notification that timed
/// out mid-operation cannot be brought back by update().
struct Notification
{
    std::string title;
    std::string message;
    NotificationLevel level = NotificationLevel::Info;

    /// Optional completion fraction in [0, 1]; sinks render it as a gauge.
    std::optional<float> progress;

    std::chrono::milliseconds timeout = std::chrono::seconds(6);

    /// When true and progress reaches 1.0 while the timeout is sticky, Notifier
    /// substitutes completion_dismiss_timeout so the finished notification fades
    /// out on its own. Set to false when reaching 1.0 is not the end of the
    /// operation and the notification must stay updatable.
    bool dismiss_when_done = true;
};

/// Sticky (never-expiring) progress notification: the standard shape for
/// reporting a long-running operation through a NotificationHandle.
Notification make_progress_notification(std::string title, std::string message, float progress);

/// Delivery interface, implemented once per UI toolkit. Implementations must be
/// thread-safe (notifications arrive from the UI thread, the watcher thread and
/// background open tasks) and must not extend UI lifetime: Notifier copies live
/// in long-lived model objects, so a sink typically outlives the widgets it
/// draws into and has to degrade to a no-op (show() returning 0) once they are
/// gone, rather than keeping them alive.
class NotificationSink
{
public:
    virtual ~NotificationSink() = default;

    virtual NotificationId show(Notification notification) = 0;

    /// Updating an unknown id (never shown, dismissed, or expired) is a no-op.
    virtual void update(NotificationId id, Notification notification) = 0;
    virtual void dismiss(NotificationId id)                           = 0;
};

/// Cheap copyable value handle through which all application code posts
/// notifications. A default-constructed Notifier silently drops everything,
/// which keeps call sites unconditional. Applies the dismiss_when_done
/// completion policy before forwarding, so auto-dismiss of finished progress
/// does not depend on the sink. Thread-safe whenever the sink is.
class Notifier
{
public:
    Notifier() = default;
    explicit Notifier(std::shared_ptr<NotificationSink> sink);

    [[nodiscard]] NotificationId show(Notification notification) const;
    void update(NotificationId id, Notification notification) const;
    void dismiss(NotificationId id) const;

    /// Fire-and-forget notifications with the default timeout (errors use the
    /// longer error_notification_timeout).
    void info(std::string title, std::string message) const;
    void success(std::string title, std::string message) const;
    void warning(std::string title, std::string message) const;
    void error(std::string title, std::string message) const;

private:
    std::shared_ptr<NotificationSink> _sink;
};

/// Follows one logical notification through show -> update(s) -> completion, so
/// a long-running operation keeps reusing a single toast. The first
/// show_or_update shows, later calls update in place — unless the previously
/// sent notification expires on its own (positive timeout, or completed
/// progress): updating an expired id would be silently dropped by the sink, so
/// the handle starts a fresh notification instead. Not thread-safe; confine a
/// handle to one thread/operation.
class NotificationHandle
{
public:
    NotificationHandle() = default;
    explicit NotificationHandle(Notifier notifier);

    void show_or_update(Notification notification);
    void dismiss();
    [[nodiscard]] NotificationId id() const;

private:
    Notifier _notifier;
    NotificationId _id      = 0;
    bool _last_sent_expires = false;
};

} // namespace slayerlog
