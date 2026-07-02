#pragma once

#include <string_view>

#include "command_context.hpp"
#include "core_command.hpp"

namespace slayerlog
{

class CommandPaletteSession;

/// Opens the palette's close-open-file picker mode on the core
/// CommandPaletteSession; the confirm handler closes the selected tracked
/// source and reloads the view. Stateless: the interaction lives in the
/// session, so every UI renders it from the shared palette model.
class CloseOpenFileCommand final : public CoreCommand
{
public:
    CloseOpenFileCommand(CommandContext context, CommandPaletteSession& palette_session);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    CommandPaletteSession& _palette_session;
};

} // namespace slayerlog
