#pragma once

#include "command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class ResetFiltersCommand final : public Command
{
public:
    explicit ResetFiltersCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
private:
    CommandContext _context;
};

} // namespace slayerlog
