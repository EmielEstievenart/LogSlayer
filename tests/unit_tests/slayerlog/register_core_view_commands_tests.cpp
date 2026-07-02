#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "command_manager.hpp"
#include "command_palette_model.hpp"
#include "command_palette_session.hpp"
#include "null_log_view_service.hpp"
#include "register_core_view_commands.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

std::vector<std::string> registered_command_names(const CommandManager& command_manager)
{
    std::vector<std::string> names;
    for (const auto& descriptor : command_manager.commands())
    {
        names.push_back(descriptor.name);
    }

    return names;
}

// The full palette order the TUI has always shown: the pure core commands with
// the interactive pickers interleaved at their historical positions.
const std::vector<std::string> interleaved_command_names {
    "filter-in",          "filter-out",         "reset-filters",        "delete-filters",       "clear-filters",    "hide-columns",        "reset-column-filter", "clear-column-filters",
    "show-original-time", "hide-original-time", "show-identical-lines", "hide-identical-lines", "open-file",        "open-folder",         "close-open-file",     "set-time-format",
    "adjust-time-offset", "clear-time-offset",  "go-to-line",           "hide-before-line",     "hide-shown-lines", "export-visible-text", "copy-settings-path",
};

} // namespace

TEST(RegisterCoreViewCommandsTest, RegistersFullCommandSetInHistoricalPaletteOrder)
{
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    NullLogViewService log_view;
    CommandManager command_manager;
    CommandPaletteModel palette_model;
    CommandPaletteSession palette_session(palette_model, command_manager);

    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, palette_session);

    EXPECT_EQ(registered_command_names(command_manager), interleaved_command_names);
}

} // namespace slayerlog
