#include "commands/session_replay.hpp"

#include <algorithm>
#include <cctype>

#include "commands/command_manager.hpp"
#include "implementations/open_command.hpp"

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

std::string lowercase_command_name(std::string_view command_line)
{
    const std::size_t name_end = command_line.find_first_of(" \t");
    std::string name(command_line.substr(0, name_end));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return name;
}

std::string arguments_of(std::string_view command_line)
{
    const std::size_t name_end = command_line.find_first_of(" \t");
    if (name_end == std::string_view::npos)
    {
        return {};
    }

    return trim_text(command_line.substr(name_end + 1));
}

} // namespace

SessionReplayReport replay_session_commands(const std::vector<std::string>& command_lines, CommandManager& command_manager, const CommandContext& context)
{
    // Opens run through the commands' synchronous path (no model mutex / task
    // runner in the context) so a source exists before the next line uses it.
    CommandContext synchronous_context   = context;
    synchronous_context.model_mutex      = nullptr;
    synchronous_context.background_tasks = nullptr;
    OpenCommand open_command(synchronous_context);
    OpenFileCommand open_file_command(synchronous_context);
    OpenFolderCommand open_folder_command(synchronous_context);

    SessionReplayReport report;
    for (const auto& raw_line : command_lines)
    {
        const std::string command_line = trim_text(raw_line);
        if (command_line.empty())
        {
            continue;
        }

        ++report.executed_count;
        const std::string name = lowercase_command_name(command_line);

        CommandResult result;
        if (name == "open")
        {
            result = open_command.execute(arguments_of(command_line));
        }
        else if (name == "open-file")
        {
            result = open_file_command.execute(arguments_of(command_line));
        }
        else if (name == "open-folder")
        {
            result = open_folder_command.execute(arguments_of(command_line));
        }
        else
        {
            result = command_manager.execute(command_line);
            if (command_manager.active_command() != nullptr)
            {
                command_manager.cancel_active_command();
                report.errors.push_back(command_line + ": needs interactive input and was skipped");
                continue;
            }
        }

        if (!result.success)
        {
            report.errors.push_back(command_line + ": " + result.message);
        }
    }

    return report;
}

} // namespace slayerlog
