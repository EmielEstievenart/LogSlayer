#include "command_manager.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <utility>

namespace slayerlog
{

namespace
{

class LambdaCommand final : public Command
{
public:
    LambdaCommand(CommandDescriptor descriptor, CommandHandler handler, CommandPreviewHandler preview_handler) : _descriptor(std::move(descriptor)), _handler(std::move(handler)), _preview_handler(std::move(preview_handler)) { }

    const CommandDescriptor& descriptor() const override
    {
        return _descriptor;
    }

    CommandResult execute(std::string_view arguments) override
    {
        return _handler(arguments);
    }

    std::optional<HiddenColumnRange> hidden_column_preview(std::string_view arguments) const override
    {
        if (!_preview_handler)
        {
            return std::nullopt;
        }

        return _preview_handler(arguments);
    }

private:
    CommandDescriptor _descriptor;
    CommandHandler _handler;
    CommandPreviewHandler _preview_handler;
};

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

void CommandManager::register_command(std::unique_ptr<Command> command)
{
    if (command == nullptr)
    {
        throw std::invalid_argument("Command must not be null");
    }

    const CommandDescriptor& descriptor = command->descriptor();
    const std::string trimmed_name      = trim(descriptor.name);
    if (trimmed_name.empty())
    {
        throw std::invalid_argument("Command name must not be empty");
    }

    const std::string normalized_name = normalize_command_name(trimmed_name);
    if (find_command(normalized_name) != nullptr)
    {
        throw std::invalid_argument("Duplicate command name: " + trimmed_name);
    }

    _commands.push_back({std::move(command), normalized_name});
}

void CommandManager::register_command(CommandDescriptor descriptor, CommandHandler handler, CommandPreviewHandler preview_handler)
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

    register_command(std::make_unique<LambdaCommand>(std::move(descriptor), std::move(handler), std::move(preview_handler)));
}

std::vector<CommandDescriptor> CommandManager::commands() const
{
    std::vector<CommandDescriptor> descriptors;
    descriptors.reserve(_commands.size());

    for (const auto& command : _commands)
    {
        descriptors.push_back(command.command->descriptor());
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
            matches.push_back(command.command->descriptor());
        }
    }

    return matches;
}

CommandResult CommandManager::execute(std::string_view command_line)
{
    cancel_active_command();

    const ParsedCommandLine parsed = parse_command_line(command_line);
    if (parsed.original.empty())
    {
        return {false, "Enter a command."};
    }

    RegisteredCommand* command = find_command(parsed.normalized_name);
    if (command == nullptr)
    {
        return {false, "Unknown command: " + parsed.name};
    }

    CommandResult result = command->command->execute(parsed.arguments);
    if (result.success && command->command->has_active_interaction())
    {
        _active_command = command->command.get();
    }
    else
    {
        _active_command = nullptr;
    }

    return result;
}

Command* CommandManager::active_command()
{
    return _active_command;
}

const Command* CommandManager::active_command() const
{
    return _active_command;
}

void CommandManager::clear_active_command()
{
    _active_command = nullptr;
}

void CommandManager::cancel_active_command()
{
    if (_active_command != nullptr)
    {
        _active_command->cancel();
        _active_command = nullptr;
    }
}

std::optional<HiddenColumnRange> CommandManager::hidden_column_preview(std::string_view command_line) const
{
    const ParsedCommandLine parsed = parse_command_line(command_line);
    if (parsed.original.empty())
    {
        return std::nullopt;
    }

    const RegisteredCommand* command = find_command(parsed.normalized_name);
    if (command == nullptr)
    {
        return std::nullopt;
    }

    return command->command->hidden_column_preview(parsed.arguments);
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

ParsedCommandLine CommandManager::parse_command_line(std::string_view command_line)
{
    ParsedCommandLine parsed;
    parsed.original = trim(command_line);
    if (parsed.original.empty())
    {
        return parsed;
    }

    parsed.name            = typed_command_name(parsed.original);
    parsed.normalized_name = normalize_command_name(parsed.name);

    const std::size_t argument_offset = parsed.original.find_first_of(" \t\r\n");
    if (argument_offset != std::string::npos)
    {
        parsed.arguments = trim(trim_view(std::string_view(parsed.original).substr(argument_offset + 1)));
    }

    return parsed;
}

CommandManager::RegisteredCommand* CommandManager::find_command(std::string_view name)
{
    const std::string normalized_name = normalize_command_name(name);
    const auto match                  = std::find_if(_commands.begin(), _commands.end(), [&](const RegisteredCommand& command) { return command.normalized_name == normalized_name; });

    return match == _commands.end() ? nullptr : &*match;
}

const CommandManager::RegisteredCommand* CommandManager::find_command(std::string_view name) const
{
    const std::string normalized_name = normalize_command_name(name);
    const auto match                  = std::find_if(_commands.begin(), _commands.end(), [&](const RegisteredCommand& command) { return command.normalized_name == normalized_name; });

    return match == _commands.end() ? nullptr : &*match;
}

} // namespace slayerlog
