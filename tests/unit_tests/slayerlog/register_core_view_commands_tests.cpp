#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_manager.hpp"
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

class FakePickerCommand final : public CoreCommand
{
public:
    explicit FakePickerCommand(std::string name) : _descriptor {std::move(name), "fake picker", "usage"} { }

    const CommandDescriptor& descriptor() const override { return _descriptor; }
    CommandResult execute(std::string_view) override { return {true, "ok"}; }

private:
    CommandDescriptor _descriptor;
};

std::string picker_slot_name(ViewPickerCommandSlot slot)
{
    switch (slot)
    {
    case ViewPickerCommandSlot::DeleteFilters:
        return "delete-filters";
    case ViewPickerCommandSlot::CloseOpenFile:
        return "close-open-file";
    case ViewPickerCommandSlot::SetTimeFormat:
        return "set-time-format";
    case ViewPickerCommandSlot::AdjustTimeOffset:
        return "adjust-time-offset";
    case ViewPickerCommandSlot::ClearTimeOffset:
        return "clear-time-offset";
    }

    return {};
}

const std::vector<std::string> pure_core_command_names {
    "filter-in", "filter-out",  "reset-filters", "clear-filters",    "hide-columns",     "reset-column-filter", "clear-column-filters", "show-original-time", "hide-original-time", "show-identical-lines", "hide-identical-lines",
    "open-file", "open-folder", "go-to-line",    "hide-before-line", "hide-shown-lines", "export-visible-text", "copy-settings-path",
};

// The full palette order the TUI has always shown: the pure core commands with
// the interactive pickers interleaved at their historical positions.
const std::vector<std::string> interleaved_command_names {
    "filter-in",          "filter-out",         "reset-filters",        "delete-filters",       "clear-filters",    "hide-columns",        "reset-column-filter", "clear-column-filters",
    "show-original-time", "hide-original-time", "show-identical-lines", "hide-identical-lines", "open-file",        "open-folder",         "close-open-file",     "set-time-format",
    "adjust-time-offset", "clear-time-offset",  "go-to-line",           "hide-before-line",     "hide-shown-lines", "export-visible-text", "copy-settings-path",
};

} // namespace

TEST(RegisterCoreViewCommandsTest, RegistersPureCoreCommandsInPaletteOrderWithoutPickerFactory)
{
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    NullLogViewService log_view;
    CommandManager command_manager;

    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources});

    EXPECT_EQ(registered_command_names(command_manager), pure_core_command_names);
}

TEST(RegisterCoreViewCommandsTest, InterleavesPickerCommandsAtTheirHistoricalPalettePositions)
{
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    NullLogViewService log_view;
    CommandManager command_manager;

    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources}, [](ViewPickerCommandSlot slot) { return std::make_unique<FakePickerCommand>(picker_slot_name(slot)); });

    EXPECT_EQ(registered_command_names(command_manager), interleaved_command_names);
}

TEST(RegisterCoreViewCommandsTest, SkipsPickerSlotsTheFactoryDeclines)
{
    AllProcessedSources processed_sources;
    AllTrackedSources tracked_sources;
    NullLogViewService log_view;
    CommandManager command_manager;

    register_core_view_commands(command_manager, {processed_sources, log_view, tracked_sources},
                                [](ViewPickerCommandSlot slot) -> std::unique_ptr<CoreCommand>
                                {
                                    if (slot == ViewPickerCommandSlot::CloseOpenFile)
                                    {
                                        return std::make_unique<FakePickerCommand>(picker_slot_name(slot));
                                    }

                                    return nullptr;
                                });

    std::vector<std::string> expected = pure_core_command_names;
    expected.insert(expected.begin() + 13, "close-open-file");
    EXPECT_EQ(registered_command_names(command_manager), expected);
}

} // namespace slayerlog
