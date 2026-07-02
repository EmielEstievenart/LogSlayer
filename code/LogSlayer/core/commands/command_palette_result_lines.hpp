#pragma once

#include <string>
#include <vector>

#include "command_palette_model.hpp"

namespace slayerlog
{

/// The palette's result list rendered to plain strings, shared by every UI so
/// the lists cannot drift apart. Each line carries the index of the palette
/// entry it belongs to (commands span two lines: summary + usage), or -1 for
/// informational lines that are not selectable entries.
struct CommandPaletteResultLines
{
    std::vector<std::string> lines;
    std::vector<int> entry_indices;
};

/// Builds the result-list strings for the palette's current mode: command
/// matches (name + summary line, then usage line), history entries, the
/// single/multi-select picker rows, the timestamp-offset help text, and the
/// per-mode empty-state message.
CommandPaletteResultLines build_command_palette_result_lines(const CommandPaletteModel& model);

} // namespace slayerlog
