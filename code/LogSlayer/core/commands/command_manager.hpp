#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_command.hpp"

namespace slayerlog
{

using CommandHandler = std::function<CommandResult(std::string_view arguments)>;
using CommandPreviewHandler = std::function<std::optional<HiddenColumnRange>(std::string_view arguments)>;

struct ParsedCommandLine
{
    std::string original;
    std::string name;
    std::string normalized_name;
    std::string arguments;
};

class CommandManager
{
public:
    void register_command(std::unique_ptr<CoreCommand> command);
    void register_command(CommandDescriptor descriptor, CommandHandler handler, CommandPreviewHandler preview_handler = {});

    std::vector<CommandDescriptor> commands() const;
    std::vector<CommandDescriptor> matching_commands(std::string_view query) const;
    CommandResult execute(std::string_view command_line);

    CoreCommand* active_command();
    const CoreCommand* active_command() const;
    void clear_active_command();
    void cancel_active_command();

    std::optional<HiddenColumnRange> hidden_column_preview(std::string_view command_line) const;

private:
    struct RegisteredCommand
    {
        std::unique_ptr<CoreCommand> command;
        std::string normalized_name;
    };

    static std::string normalize_command_name(std::string_view name);
    static std::string trim(std::string_view text);
    static std::string typed_command_name(std::string_view query);
    static ParsedCommandLine parse_command_line(std::string_view command_line);

    RegisteredCommand* find_command(std::string_view name);
    const RegisteredCommand* find_command(std::string_view name) const;

    std::vector<RegisteredCommand> _commands;
    CoreCommand* _active_command = nullptr;
};

} // namespace slayerlog
