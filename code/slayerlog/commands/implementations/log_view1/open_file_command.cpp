#include "implementations/open_file_command.hpp"

#include <chrono>
#include <cctype>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_controller.hpp"
#include "log_source.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace slayerlog
{
namespace
{
std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

void show_file_opened_notification(const Notifier& notifier, const LogSource& source)
{
    Notification notification;
    notification.title   = "File opened";
    notification.message = source_display_path(source);
    notification.level   = NotificationLevel::Success;
    (void)notifier.show(std::move(notification));
}

void show_file_open_failed_notification(const Notifier& notifier, const std::string& message)
{
    Notification notification;
    notification.title   = "File open failed";
    notification.message = message;
    notification.level   = NotificationLevel::Error;
    notification.timeout = std::chrono::seconds(10);
    (void)notifier.show(std::move(notification));
}

Notification file_view_progress_notification(const std::string& message, NotificationLevel level, float progress)
{
    Notification notification;
    notification.title    = "Building log view";
    notification.message  = message;
    notification.level    = level;
    notification.progress = progress;
    notification.timeout  = progress >= 1.0F ? std::chrono::seconds(2) : std::chrono::milliseconds(0);
    return notification;
}

void show_source_already_open_notification(const Notifier& notifier, const LogSource& source)
{
    Notification notification;
    notification.title   = source.kind == LogSourceKind::LocalFolder ? "Folder already open" : "File already open";
    notification.message = source_basename(source);
    notification.level   = NotificationLevel::Warning;
    (void)notifier.show(std::move(notification));
}
}

OpenFileCommand::OpenFileCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& OpenFileCommand::descriptor() const
{
    static const CommandDescriptor descriptor {
        "open-file",
        "Open file and reload all tracked logs",
        "open-file <path>",
        {"Open a local file or an SSH-backed remote file and start tailing it.", "SSH sources must use ssh://user@host/absolute/path.log.", "Example: open-file logs/app.log", "Example: open-file ssh://user@example.com/var/log/app.log"}};
    return descriptor;
}

CommandResult OpenFileCommand::execute(std::string_view arguments)
{
    const std::string file_path = trim_text(arguments);
    if (file_path.empty())
    {
        return {false, "Usage: open-file <path>"};
    }

    LogSource source;
    try
    {
        source = parse_log_source(file_path);
    }
    catch (const std::exception& ex)
    {
        return {false, ex.what()};
    }

    if (_context.tracked_sources.is_source_open(source))
    {
        const std::string error = "Source already open: " + source_display_path(source);
        SLAYERLOG_LOG_ERROR("open-file failed file=" << file_path << " error=" << error);
        show_source_already_open_notification(_context.notifier, source);
        return {false, error};
    }

    if (_context.model_mutex == nullptr || _context.background_tasks == nullptr)
    {
        const auto error = open_source(_context.tracked_sources, source, _context.notifier);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("open-file failed file=" << file_path << " error=" << *error);
            return {false, *error};
        }

        reload_processed_sources(_context.tracked_sources, _context.header_text, _context.processed_sources, _context.log_controller, _context.screen);
        show_file_opened_notification(_context.notifier, source);
        return {true, "Opened file: " + source_display_path(source)};
    }

    const std::string display_path = source_display_path(source);
    auto timestamp_format_catalog  = _context.tracked_sources.timestamp_format_catalog();
    _context.background_tasks->emplace_back(
        [source = std::move(source), display_path, timestamp_format_catalog = std::move(timestamp_format_catalog), context = _context]
        {
            try
            {
                auto source_state = create_tracked_source(source, display_path, timestamp_format_catalog, context.notifier);
                source_state->poll();
                const std::size_t entry_count = source_state->entries().size();

                NotificationHandle view_progress(context.notifier);
                (void)view_progress.show_or_update(file_view_progress_notification("Preparing " + std::to_string(entry_count) + " log lines", NotificationLevel::Info, 0.0F));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                {
                    std::lock_guard lock(*context.model_mutex);
                    (void)view_progress.show_or_update(file_view_progress_notification("Adding source to model", NotificationLevel::Info, 0.25F));
                    const auto error = adopt_opened_source(context.tracked_sources, std::move(source_state));
                    if (error.has_value())
                    {
                        SLAYERLOG_LOG_ERROR("open-file failed file=" << display_path << " error=" << *error);
                        (void)view_progress.show_or_update(file_view_progress_notification(*error, NotificationLevel::Error, 1.0F));
                        show_file_open_failed_notification(context.notifier, *error);
                        return;
                    }
                    (void)view_progress.show_or_update(file_view_progress_notification("Rendering " + std::to_string(entry_count) + " log lines", NotificationLevel::Info, 0.75F));
                    reload_processed_sources(context.tracked_sources, context.header_text, context.processed_sources, context.log_controller, context.screen);
                }
                (void)view_progress.show_or_update(file_view_progress_notification("Ready " + std::to_string(entry_count) + " log lines", NotificationLevel::Success, 1.0F));
                show_file_opened_notification(context.notifier, source);
            }
            catch (const std::exception& ex)
            {
                SLAYERLOG_LOG_ERROR("open-file failed file=" << display_path << " error=" << ex.what());
                show_file_open_failed_notification(context.notifier, ex.what());
            }
        });

    return {true, "Opening file in background: " + display_path};
}

} // namespace slayerlog
