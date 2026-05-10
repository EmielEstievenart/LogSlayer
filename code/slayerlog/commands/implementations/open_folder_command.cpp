#include "implementations/open_folder_command.hpp"

#include <chrono>
#include <cctype>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
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
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) { ++start; }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) { --end; }
    return std::string(text.substr(start, end - start));
}

void show_source_already_open_notification(const Notifier& notifier, const LogSource& source)
{
    Notification notification;
    notification.title = source.kind == LogSourceKind::LocalFolder ? "Folder already open" : "File already open";
    notification.message = source_basename(source);
    notification.level = NotificationLevel::Warning;
    (void)notifier.show(std::move(notification));
}

void finish_folder_open_notification(TrackedSourceBase* source_state, const Notifier& notifier, const std::string& title, const std::string& message, NotificationLevel level, float progress)
{
    if (auto* folder_source = dynamic_cast<TrackedSourceFolder*>(source_state))
    {
        folder_source->finish_open_notification(title, message, level, progress);
        return;
    }

    Notification notification;
    notification.title = title;
    notification.message = message;
    notification.level = level;
    notification.progress = progress;
    notification.timeout = std::chrono::seconds(level == NotificationLevel::Error ? 10 : 6);
    (void)notifier.show(std::move(notification));
}
}

OpenFolderCommand::OpenFolderCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& OpenFolderCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"open-folder", "Open folder and reload all tracked logs", "open-folder <path>", {"Watch every regular file in a local folder and include new files as they appear.", "Supported file types include plain text logs, .zst compressed logs, and Vector .blf CAN/CAN-FD logs.", "BLF support requires Python and vblf: python -m pip install vblf", "Use this for log directories; SSH folders are not supported.", "Example: open-folder logs/archive"}};
    return descriptor;
}

CommandResult OpenFolderCommand::execute(std::string_view arguments)
{
    const std::string folder_path = trim_text(arguments);
    if (folder_path.empty()) { return {false, "Usage: open-folder <path>"}; }

    LogSource source;
    try { source = make_local_folder_source(folder_path); }
    catch (const std::exception& ex) { return {false, ex.what()}; }

    const std::string display_path = source_display_path(source);
    if (_context.tracked_sources.is_source_open(source))
    {
        const std::string error = "Source already open: " + display_path;
        SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << error);
        show_source_already_open_notification(_context.notifier, source);
        return {false, error};
    }

    if (_context.model_mutex == nullptr || _context.background_tasks == nullptr)
    {
        const auto error = _context.tracked_sources.open_source(source, _context.notifier);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("open-folder failed folder=" << source_display_path(source) << " error=" << *error);
            return {false, *error};
        }

        reload_processed_sources(_context.tracked_sources, _context.header_text, _context.processed_sources, _context.log_controller, _context.screen);
        return {true, "Opened folder: " + source_display_path(source)};
    }

    auto timestamp_format_catalog = _context.tracked_sources.timestamp_format_catalog();
    _context.background_tasks->emplace_back([source = std::move(source), display_path, timestamp_format_catalog = std::move(timestamp_format_catalog), context = _context]
                                            {
                                                std::unique_ptr<TrackedSourceBase> source_state;
                                                try
                                                {
                                                    source_state = create_tracked_source(source, display_path, timestamp_format_catalog, context.notifier);
                                                    source_state->poll();
                                                    TrackedSourceBase* opened_source = source_state.get();
                                                    {
                                                        std::lock_guard lock(*context.model_mutex);
                                                        const auto error = context.tracked_sources.add_opened_source(std::move(source_state));
                                                        if (error.has_value())
                                                        {
                                                            SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << *error);
                                                            finish_folder_open_notification(nullptr, context.notifier, "Folder open failed", *error, NotificationLevel::Error, 1.0F);
                                                            return;
                                                        }
                                                        reload_processed_sources(context.tracked_sources, context.header_text, context.processed_sources, context.log_controller, context.screen);
                                                    }
                                                    finish_folder_open_notification(opened_source, context.notifier, "Folder opened", display_path, NotificationLevel::Success, 1.0F);
                                                }
                                                catch (const std::exception& ex)
                                                {
                                                    SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << ex.what());
                                                    finish_folder_open_notification(source_state.get(), context.notifier, "Folder open failed", ex.what(), NotificationLevel::Error, 1.0F);
                                                }
                                            });

    return {true, "Opening folder in background: " + display_path};
}

} // namespace slayerlog
