#pragma once

#include <string_view>

#include "command_context.hpp"
#include "core_command.hpp"

namespace slayerlog
{

class CommandPaletteSession;

/// Two-stage flow on the core CommandPaletteSession: a source picker (labels
/// annotated with each source's current offset), then the timestamp-offset
/// input mode with its live parse preview; the confirm adds the entered offset
/// on top of the source's current offset and reloads the view. Stateless: the
/// interaction lives in the session, so every UI renders it from the shared
/// palette model.
class AdjustTimeOffsetCommand final : public CoreCommand
{
public:
    AdjustTimeOffsetCommand(CommandContext context, CommandPaletteSession& palette_session);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    CommandPaletteSession& _palette_session;
};

} // namespace slayerlog
