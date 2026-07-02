#pragma once

#include <string_view>

#include "command_context.hpp"
#include "core_command.hpp"

namespace slayerlog
{

class CommandPaletteSession;

/// Opens the palette's multi-select delete-filters picker mode on the core
/// CommandPaletteSession; the confirm handler removes the marked filters from
/// the processed sources. The command itself is stateless: the whole
/// interaction lives in the session, so every UI renders it from the shared
/// palette model.
class DeleteFiltersCommand final : public CoreCommand
{
public:
    DeleteFiltersCommand(CommandContext context, CommandPaletteSession& palette_session);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    CommandPaletteSession& _palette_session;
};

} // namespace slayerlog
