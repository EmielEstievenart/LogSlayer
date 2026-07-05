#pragma once

#include "command_context.hpp"
#include "commands/core_command.hpp"

namespace slayerlog
{

class CommandManager;

class LoadConfigCommand final : public CoreCommand
{
public:
    LoadConfigCommand(CommandContext context, CommandManager& command_manager);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    CommandManager& _command_manager;

    /// Guards against a config whose command list contains load-config itself.
    bool _replaying = false;
};

} // namespace slayerlog
