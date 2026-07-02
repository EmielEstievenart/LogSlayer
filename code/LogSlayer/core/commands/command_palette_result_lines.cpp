#include "command_palette_result_lines.hpp"

#include <cstddef>
#include <utility>

namespace slayerlog
{

CommandPaletteResultLines build_command_palette_result_lines(const CommandPaletteModel& model)
{
    CommandPaletteResultLines result;

    auto push_line = [&result](std::string line, int entry_index)
    {
        result.lines.push_back(std::move(line));
        result.entry_indices.push_back(entry_index);
    };

    if (model.mode == CommandPaletteMode::History)
    {
        if (model.matching_history_entries.empty())
        {
            const std::string empty_message = model.query.empty() ? "No previously run commands" : "No matching history commands";
            push_line(empty_message, -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.matching_history_entries.size(); ++index)
            {
                push_line(model.matching_history_entries[index], static_cast<int>(index));
            }
        }
    }
    else if (model.mode == CommandPaletteMode::CloseOpenFile)
    {
        if (model.open_files.empty())
        {
            push_line("No open files", -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.open_files.size(); ++index)
            {
                push_line(model.open_files[index], static_cast<int>(index));
            }
        }
    }
    else if (model.mode == CommandPaletteMode::SelectTimestampSource)
    {
        if (model.open_files.empty())
        {
            push_line("No open sources", -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.open_files.size(); ++index)
            {
                push_line(model.open_files[index], static_cast<int>(index));
            }
        }
    }
    else if (model.mode == CommandPaletteMode::SelectTimestampFormat)
    {
        if (model.timestamp_formats.empty())
        {
            push_line("No timestamp formats configured", -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.timestamp_formats.size(); ++index)
            {
                push_line(model.timestamp_formats[index], static_cast<int>(index));
            }
        }
    }
    else if (model.mode == CommandPaletteMode::EnterTimestampOffset)
    {
        push_line("Source: " + model.timestamp_offset_source_label, -1);
        push_line("Expected: DD hh:mm:ss[.fraction]", -1);
        push_line("Example: 20 02:10:10.005", -1);
        if (model.timestamp_offset_preview.empty())
        {
            push_line("Enter an offset", -1);
        }
        else
        {
            push_line(model.timestamp_offset_preview, -1);
        }
    }
    else if (model.mode == CommandPaletteMode::DeleteFilters)
    {
        if (model.filter_picker_entries.empty())
        {
            push_line("No filters configured", -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.filter_picker_entries.size(); ++index)
            {
                const auto& entry        = model.filter_picker_entries[index];
                const std::string prefix = entry.selected ? "[x] " : "[ ] ";
                const std::string tag    = entry.include ? "(in) " : "(out) ";
                push_line(prefix + tag + entry.label, static_cast<int>(index));
            }
        }
    }
    else
    {
        if (model.matching_commands.empty())
        {
            push_line("No matching commands", -1);
        }
        else
        {
            for (std::size_t index = 0; index < model.matching_commands.size(); ++index)
            {
                const auto& command = model.matching_commands[index];
                push_line(command.name + " - " + command.summary, static_cast<int>(index));
                push_line(command.usage, static_cast<int>(index));
            }
        }
    }

    return result;
}

} // namespace slayerlog
