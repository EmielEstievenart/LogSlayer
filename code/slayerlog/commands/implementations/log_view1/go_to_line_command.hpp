#pragma once

#include "command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class GoToLineCommand final : public Command
{
public:
    explicit GoToLineCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
private:
    CommandContext _context;
};

} // namespace slayerlog
