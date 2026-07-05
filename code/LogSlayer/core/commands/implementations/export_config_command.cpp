#include "implementations/export_config_command.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "clipboard.hpp"
#include "executable_path.hpp"
#include "session_script.hpp"
#include "session_snapshot.hpp"

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

bool default_clipboard_writer(const std::string& text, std::string& error_message)
{
    if (!CopyTextToClipboard(text))
    {
        error_message = "Failed to copy to clipboard";
        return false;
    }
    return true;
}

std::string default_executable_resolver()
{
    const auto path = executable_path();
    return path.empty() ? std::string("LogSlayer") : path.string();
}

} // namespace

ExportConfigCommand::ExportConfigCommand(CommandContext context) : ExportConfigCommand(context, default_clipboard_writer, default_executable_resolver)
{
}

ExportConfigCommand::ExportConfigCommand(CommandContext context, ClipboardWriter clipboard_writer, ExecutableResolver executable_resolver)
    : _context(context), _clipboard_writer(std::move(clipboard_writer)), _executable_resolver(std::move(executable_resolver))
{
}

const CommandDescriptor& ExportConfigCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"export-config",
                                               "Export the session as a runnable script or command line",
                                               "export-config [<path>.bat|.cmd|.sh]",
                                               {"Snapshots the session like save-config, but as something you can run outside LogSlayer.", "With a path, writes a batch (.bat/.cmd) or shell (.sh) script that relaunches this setup.",
                                                "Without a path, copies the equivalent single command line to the clipboard.", "Example: export-config crashhunt.bat"}};
    return descriptor;
}

CommandResult ExportConfigCommand::execute(std::string_view arguments)
{
    const auto command_lines     = serialize_session_commands(_context.tracked_sources, _context.processed_sources, &_context.log_view);
    const std::string executable = _executable_resolver();

    const std::string path_text = trim_text(arguments);
    if (path_text.empty())
    {
        const std::string invocation = build_session_invocation_line(native_session_script_kind(), executable, command_lines);
        std::string error_message;
        if (!_clipboard_writer(invocation, error_message))
        {
            return {false, error_message};
        }

        return {true, "Copied command line to clipboard (" + std::to_string(command_lines.size()) + " commands)"};
    }

    const std::filesystem::path script_path(path_text);
    const auto script_kind = session_script_kind_for_path(script_path);
    if (!script_kind.has_value())
    {
        return {false, "Unsupported script extension (use .bat, .cmd, or .sh): " + path_text};
    }

    const std::string script = build_session_script(*script_kind, executable, command_lines);
    std::ofstream output(script_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        return {false, "Failed to open file for writing: " + path_text};
    }

    output << script;
    output.close();
    if (!output)
    {
        return {false, "Failed to write script: " + path_text};
    }

    if (*script_kind == SessionScriptKind::PosixShell)
    {
        std::error_code error_code;
        std::filesystem::permissions(script_path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec, std::filesystem::perm_options::add, error_code);
    }

    return {true, "Exported config to " + path_text + " (" + std::to_string(command_lines.size()) + " commands)"};
}

} // namespace slayerlog
