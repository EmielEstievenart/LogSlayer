#pragma once

#include "command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class HideIdenticalLinesCommand final : public Command
{
public:
    explicit HideIdenticalLinesCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
private:
    CommandContext _context;
};

} // namespace slayerlog
