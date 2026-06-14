#pragma once

#include "core_command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class HideOriginalTimeCommand final : public CoreCommand
{
public:
    explicit HideOriginalTimeCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
private:
    CommandContext _context;
};

} // namespace slayerlog
