#pragma once

#include <functional>
#include <memory>

#include "command_context.hpp"
#include "core_command.hpp"

namespace slayerlog
{

class CommandManager;

/// The interactive picker commands that interleave with the pure core commands
/// in the palette's registration order. Each UI registrar supplies a factory
/// that builds its framework-specific implementation for a slot; returning
/// nullptr (or passing no factory at all) skips the slot, so UIs without
/// picker widgets simply do not list those commands yet.
enum class ViewPickerCommandSlot
{
    DeleteFilters,
    CloseOpenFile,
    SetTimeFormat,
    AdjustTimeOffset,
    ClearTimeOffset,
};

using ViewPickerCommandFactory = std::function<std::unique_ptr<CoreCommand>(ViewPickerCommandSlot slot)>;

/// Registers the pure core commands common to any log view: everything that
/// drives the processed or tracked sources through the UI-agnostic
/// CommandContext. The picker factory is invoked at the exact positions the
/// interactive picker commands occupy in the palette, so a UI that supplies
/// all pickers reproduces the historical registration (and palette) order
/// unchanged. Find and time alignment are view-specific and registered by the
/// per-view entry points.
void register_core_view_commands(CommandManager& command_manager, const CommandContext& context, const ViewPickerCommandFactory& picker_factory = {});

} // namespace slayerlog
