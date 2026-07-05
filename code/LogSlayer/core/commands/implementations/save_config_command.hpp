#pragma once

#include "command_context.hpp"
#include "commands/core_command.hpp"

namespace slayerlog
{

class SaveConfigCommand final : public CoreCommand
{
public:
    explicit SaveConfigCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
};

} // namespace slayerlog
