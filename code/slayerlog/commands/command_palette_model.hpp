#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "command_manager.hpp"
#include "log_types.hpp"

namespace slayerlog
{

enum class CommandPaletteMode
{
    Commands,
    History,
    CloseOpenFile,
    SelectTimestampSource,
    SelectTimestampFormat,
    EnterTimestampOffset,
    DeleteFilters,
};

struct CommandPaletteModel
{
    struct FilterPickerEntry
    {
        std::string label;
        bool include             = true;
        std::size_t filter_index = 0;
        bool selected            = false;
    };

    bool open               = false;
    CommandPaletteMode mode = CommandPaletteMode::Commands;
    std::string query;
    std::size_t cursor_position = 0;
    std::vector<CommandDescriptor> matching_commands;
    std::vector<std::string> matching_history_entries;
    std::vector<std::string> open_files;
    std::vector<std::string> timestamp_formats;
    std::vector<FilterPickerEntry> filter_picker_entries;
    std::string timestamp_offset_source_label;
    std::string timestamp_offset_preview;
    bool timestamp_offset_preview_is_error = false;
    int selected_index = 0;
    std::string status_message;
    bool status_is_error = false;
    std::optional<HiddenColumnRange> hidden_column_preview;
};

} // namespace slayerlog
