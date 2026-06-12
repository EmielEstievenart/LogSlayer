#include "implementations/open_command.hpp"

#include <cctype>
#include <filesystem>
#include <string>

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

bool is_ssh_source(std::string_view path)
{
    constexpr std::string_view ssh_scheme = "ssh://";
    return path.rfind(ssh_scheme, 0) == 0;
}

} // namespace

OpenCommand::OpenCommand(CommandContext context) : _open_file_command(context), _open_folder_command(context)
{
}

const CommandDescriptor& OpenCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"open",
                                               "Open a file or folder",
                                               "open <path>",
                                               {"Open a local file, local folder, or SSH-backed remote file.", "Local folders are watched as folders; local files and SSH sources are opened as files.", "Example: open logs/app.log",
                                                "Example: open logs/archive", "Example: open ssh://user@example.com/var/log/app.log"}};
    return descriptor;
}

CommandResult OpenCommand::execute(std::string_view arguments)
{
    const std::string path = trim_text(arguments);
    if (path.empty())
    {
        return {false, "Usage: open <path>"};
    }

    if (is_ssh_source(path))
    {
        return _open_file_command.execute(path);
    }

    std::error_code error_code;
    const auto status = std::filesystem::status(path, error_code);
    if (!error_code && std::filesystem::is_directory(status))
    {
        return _open_folder_command.execute(path);
    }

    if (!error_code && std::filesystem::is_regular_file(status))
    {
        return _open_file_command.execute(path);
    }

    return {false, "Path is not a file or folder: " + path};
}

} // namespace slayerlog
