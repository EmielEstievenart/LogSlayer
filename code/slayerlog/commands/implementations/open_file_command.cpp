#include "implementations/open_file_command.hpp"

#include <cctype>
#include <exception>
#include <string>
#include <utility>

#include "command_support.hpp"
#include "debug_log.hpp"
#include "log_controller.hpp"
#include "log_source.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

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

void show_file_opened_notification(const Notifier& notifier, const LogSource& source)
{
    Notification notification;
    notification.title = "File opened";
    notification.message = source_display_path(source);
    notification.level = NotificationLevel::Success;
    (void)notifier.show(std::move(notification));
}

void show_source_already_open_notification(const Notifier& notifier, const LogSource& source)
{
    Notification notification;
    notification.title = source.kind == LogSourceKind::LocalFolder ? "Folder already open" : "File already open";
    notification.message = source_basename(source);
    notification.level = NotificationLevel::Warning;
    (void)notifier.show(std::move(notification));
}
}

OpenFileCommand::OpenFileCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& OpenFileCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"open-file", "Open file and reload all tracked logs", "open-file <path>", {"Open a local file or an SSH-backed remote file and start tailing it.", "SSH sources must use ssh://user@host/absolute/path.log.", "Example: open-file logs/app.log", "Example: open-file ssh://user@example.com/var/log/app.log"}};
    return descriptor;
}

CommandResult OpenFileCommand::execute(std::string_view arguments)
{
    const std::string file_path = trim_text(arguments);
    if (file_path.empty()) { return {false, "Usage: open-file <path>"}; }

    LogSource source;
    try { source = parse_log_source(file_path); }
    catch (const std::exception& ex) { return {false, ex.what()}; }

    if (_context.tracked_sources.is_source_open(source))
    {
        const std::string error = "Source already open: " + source_display_path(source);
        SLAYERLOG_LOG_ERROR("open-file failed file=" << file_path << " error=" << error);
        show_source_already_open_notification(_context.notifier, source);
        return {false, error};
    }

    const auto error = _context.tracked_sources.open_source(source);
    if (error.has_value())
    {
        SLAYERLOG_LOG_ERROR("open-file failed file=" << file_path << " error=" << *error);
        return {false, *error};
    }

    reload_processed_sources(_context.tracked_sources, _context.header_text, _context.processed_sources, _context.log_controller, _context.screen);
    show_file_opened_notification(_context.notifier, source);
    return {true, "Opened file: " + source_display_path(source)};
}

} // namespace slayerlog
