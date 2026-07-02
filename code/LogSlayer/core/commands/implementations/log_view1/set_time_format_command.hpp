#pragma once

#include <string_view>

#include "command_context.hpp"
#include "core_command.hpp"

namespace slayerlog
{

class CommandPaletteSession;

/// Two-stage picker flow on the core CommandPaletteSession: first a source
/// picker, then a timestamp-format picker; the final confirm reparses the
/// source with the chosen format and reloads the view. Stateless: the
/// interaction lives in the session, so every UI renders it from the shared
/// palette model.
class SetTimeFormatCommand final : public CoreCommand
{
public:
    SetTimeFormatCommand(CommandContext context, CommandPaletteSession& palette_session);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    CommandPaletteSession& _palette_session;
};

} // namespace slayerlog
