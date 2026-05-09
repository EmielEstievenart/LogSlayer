#pragma once

#include <optional>

#include "command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class HideColumnsCommand final : public Command
{
public:
    explicit HideColumnsCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
    std::optional<HiddenColumnRange> hidden_column_preview(std::string_view arguments) const override;
private:
    CommandContext _context;
};

} // namespace slayerlog
