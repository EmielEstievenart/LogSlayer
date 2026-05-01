#include "command_manager.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace slayerlog
{

namespace
{

std::string_view trim_view(std::string_view text)
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

    return text.substr(start, end - start);
}

} // namespace

void CommandManager::register_command(CommandDescriptor descriptor, CommandHandler handler)
{
    descriptor.name = trim(descriptor.name);
    if (descriptor.name.empty())
    {
        throw std::invalid_argument("Command name must not be empty");
    }

    if (!handler)
    {
        throw std::invalid_argument("Command handler must not be empty");
    }

    const std::string normalized_name = normalize_command_name(descriptor.name);
    if (find_command(normalized_name) != nullptr)
    {
        throw std::invalid_argument("Duplicate command name: " + descriptor.name);
    }

    _commands.push_back({
        std::move(descriptor),
        std::move(handler),
        normalized_name,
    });
}

std::vector<CommandDescriptor> CommandManager::commands() const
{
    std::vector<CommandDescriptor> descriptors;
    descriptors.reserve(_commands.size());

    for (const auto& command : _commands)
    {
        descriptors.push_back(command.descriptor);
    }

    return descriptors;
}

std::vector<CommandDescriptor> CommandManager::matching_commands(std::string_view query) const
{
    const std::string normalized_query = normalize_command_name(typed_command_name(query));

    std::vector<CommandDescriptor> matches;
    matches.reserve(_commands.size());
    for (const auto& command : _commands)
    {
        if (normalized_query.empty() || command.normalized_name.find(normalized_query) != std::string::npos)
        {
            matches.push_back(command.descriptor);
        }
    }

    return matches;
}

CommandResult CommandManager::execute(std::string_view command_line) const
{
    const std::string trimmed_line = trim(command_line);
    if (trimmed_line.empty())
    {
        return {false, "Enter a command."};
    }

    const std::string command_name   = typed_command_name(trimmed_line);
    const RegisteredCommand* command = find_command(command_name);
    if (command == nullptr)
    {
        return {false, "Unknown command: " + command_name};
    }

    const std::size_t argument_offset = trimmed_line.find_first_of(" \t\r\n");
    if (argument_offset == std::string::npos)
    {
        return command->handler({});
    }

    return command->handler(trim_view(std::string_view(trimmed_line).substr(argument_offset + 1)));
}

std::string CommandManager::normalize_command_name(std::string_view name)
{
    const std::string trimmed_name = trim(name);

    std::string normalized;
    normalized.reserve(trimmed_name.size());
    for (const char ch : trimmed_name)
    {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    return normalized;
}

std::string CommandManager::trim(std::string_view text)
{
    return std::string(trim_view(text));
}

std::string CommandManager::typed_command_name(std::string_view query)
{
    const std::string_view trimmed_query = trim_view(query);
    const std::size_t separator_index    = trimmed_query.find_first_of(" \t\r\n");
    if (separator_index == std::string::npos)
    {
        return std::string(trimmed_query);
    }

    return std::string(trimmed_query.substr(0, separator_index));
}

const CommandManager::RegisteredCommand* CommandManager::find_command(std::string_view name) const
{
    const std::string normalized_name = normalize_command_name(name);
    const auto match                  = std::find_if(_commands.begin(), _commands.end(), [&](const RegisteredCommand& command) { return command.normalized_name == normalized_name; });

    return match == _commands.end() ? nullptr : &*match;
}

} // namespace slayerlog
