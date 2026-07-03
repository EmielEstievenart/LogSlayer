#pragma once

#include "core_command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class HideBeforeLineCommand final : public CoreCommand
{
public:
    explicit HideBeforeLineCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
private:
    CommandContext _context;
};

} // namespace slayerlog
