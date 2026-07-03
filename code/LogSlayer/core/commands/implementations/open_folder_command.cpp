#include "implementations/open_folder_command.hpp"

#include <cctype>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "debug_log.hpp"
#include "log_view_service.hpp"
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

/// Finalizes the folder's own open-progress notification when there is one;
/// falls back to a standalone notification when the source never got that far
/// (or is not a folder).
void finish_folder_open_notification(TrackedSourceBase* source_state, const Notifier& notifier, const std::string& title, const std::string& message, NotificationLevel level)
{
    if (auto* folder_source = dynamic_cast<TrackedSourceFolder*>(source_state))
    {
        folder_source->finish_open_notification(title, message, level);
        return;
    }

    if (level == NotificationLevel::Error)
    {
        notifier.error(title, message);
        return;
    }

    notifier.success(title, message);
}

/// Creates, polls, adopts and reports one folder source. Creation and the first
/// poll (which read every file) run without the model mutex so the UI stays
/// live; adoption, the view reload and the final notification run under it when
/// one is provided. Returns std::nullopt on success, the error message on
/// failure (after notifying either way).
std::optional<std::string> open_folder_and_report(const CommandContext& context, const LogSource& source, const std::string& display_path, std::shared_ptr<const TimestampFormatCatalog> timestamp_format_catalog)
{
    std::unique_ptr<TrackedSourceBase> source_state;
    try
    {
        source_state = create_tracked_source(source, display_path, std::move(timestamp_format_catalog), context.notifier);
        source_state->poll();
        TrackedSourceBase* opened_source = source_state.get();

        std::unique_lock<std::mutex> lock;
        if (context.model_mutex != nullptr)
        {
            lock = std::unique_lock<std::mutex>(*context.model_mutex);
        }

        const auto error = adopt_opened_source(context.tracked_sources, std::move(source_state));
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << *error);
            finish_folder_open_notification(nullptr, context.notifier, "Folder open failed", *error, NotificationLevel::Error);
            return error;
        }

        context.log_view.reload(context.tracked_sources, context.processed_sources);

        // Still under the model mutex: adoption moved ownership of opened_source
        // into the model, so it is only guaranteed alive while nothing can close
        // it — do not touch it after unlocking.
        finish_folder_open_notification(opened_source, context.notifier, "Folder opened", display_path, NotificationLevel::Success);
        return std::nullopt;
    }
    catch (const std::exception& ex)
    {
        SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << ex.what());
        finish_folder_open_notification(source_state.get(), context.notifier, "Folder open failed", ex.what(), NotificationLevel::Error);
        return std::string(ex.what());
    }
}
}

OpenFolderCommand::OpenFolderCommand(CommandContext context) : _context(context)
{
}

const CommandDescriptor& OpenFolderCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"open-folder",
                                               "Open folder and reload all tracked logs",
                                               "open-folder <path>",
                                               {"Watch every regular file in a local folder and include new files as they appear.", "Supported file types include plain text logs and .zst compressed logs.",
                                                "Use this for log directories; SSH folders are not supported.", "Example: open-folder logs/archive"}};
    return descriptor;
}

CommandResult OpenFolderCommand::execute(std::string_view arguments)
{
    const std::string folder_path = trim_text(arguments);
    if (folder_path.empty())
    {
        return {false, "Usage: open-folder <path>"};
    }

    LogSource source;
    try
    {
        source = make_local_folder_source(folder_path);
    }
    catch (const std::exception& ex)
    {
        return {false, ex.what()};
    }

    const std::string display_path = source_display_path(source);
    if (_context.tracked_sources.is_source_open(source))
    {
        const std::string error = "Source already open: " + display_path;
        SLAYERLOG_LOG_ERROR("open-folder failed folder=" << display_path << " error=" << error);
        _context.notifier.warning("Folder already open", source_basename(source));
        return {false, error};
    }

    auto timestamp_format_catalog = _context.tracked_sources.timestamp_format_catalog();
    if (_context.model_mutex == nullptr || _context.background_tasks == nullptr)
    {
        const auto error = open_folder_and_report(_context, source, display_path, std::move(timestamp_format_catalog));
        if (error.has_value())
        {
            return {false, *error};
        }

        return {true, "Opened folder: " + display_path};
    }

    _context.background_tasks->emplace_back([source = std::move(source), display_path, timestamp_format_catalog = std::move(timestamp_format_catalog), context = _context]
                                            { open_folder_and_report(context, source, display_path, std::move(timestamp_format_catalog)); });

    return {true, "Opening folder in background: " + display_path};
}

} // namespace slayerlog
