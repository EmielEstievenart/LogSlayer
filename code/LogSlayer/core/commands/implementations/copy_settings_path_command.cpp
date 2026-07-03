#include "implementations/copy_settings_path_command.hpp"

#include <cctype>
#include <utility>

#include "clipboard.hpp"

#include "settings_store.hpp"

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

} // namespace

CopySettingsPathCommand::CopySettingsPathCommand(CommandContext context)
    : CopySettingsPathCommand(context,
                              [](const std::string& text, std::string& error_message)
                              {
                                  if (CopyTextToClipboard(text))
                                  {
                                      return true;
                                  }

                                  error_message = "clipboard write failed";
                                  return false;
                              })
{
}

CopySettingsPathCommand::CopySettingsPathCommand(CommandContext context, ClipboardWriter clipboard_writer) : _context(context), _clipboard_writer(std::move(clipboard_writer))
{
}

const CommandDescriptor& CopySettingsPathCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"copy-settings-path", "Copy the settings INI path to the clipboard", "copy-settings-path", {"Copies the full path of Log Slayer's settings.ini to the system clipboard.", "Use this when you want to open the settings file with your own editor or file manager."}};
    return descriptor;
}

CommandResult CopySettingsPathCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: copy-settings-path"};
    }

    const auto settings_file_path = _context.settings_file_path.empty() ? default_settings_file_path() : _context.settings_file_path;
    const std::string path_text   = settings_file_path.string();

    std::string error_message;
    if (!_clipboard_writer(path_text, error_message))
    {
        return {false, "Failed to copy settings path: " + error_message};
    }

    return {true, "Copied settings path to clipboard: " + path_text};
}

} // namespace slayerlog
