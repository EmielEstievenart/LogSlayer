#include "implementations/log_view1/delete_filters_command.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "command_palette_session.hpp"
#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

namespace
{

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

} // namespace

DeleteFiltersCommand::DeleteFiltersCommand(CommandContext context, CommandPaletteSession& palette_session) : _context(context), _palette_session(palette_session)
{
}

const CommandDescriptor& DeleteFiltersCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"delete-filters",
                                               "Delete one or more active filters",
                                               "delete-filters",
                                               {"Open a picker containing every active filter.", "Use Up/Down to select a filter, Space to mark it, and Enter to delete the marked filters.", "Each filter is labeled with (in) or (out)."}};
    return descriptor;
}

CommandResult DeleteFiltersCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: delete-filters"};
    }

    std::vector<CommandPaletteModel::FilterPickerEntry> filter_entries;
    for (const auto& filter : _context.processed_sources.all_filters())
    {
        filter_entries.push_back(CommandPaletteModel::FilterPickerEntry {filter.text, filter.include, filter.index, false});
    }

    if (filter_entries.empty())
    {
        return {false, "No filters to delete"};
    }

    _palette_session.open_delete_filters_picker(std::move(filter_entries),
                                                [this](const std::vector<CommandPaletteModel::FilterPickerEntry>& selected_filters) -> CommandResult
                                                {
                                                    std::vector<AllProcessedSources::FilterSelection> filters_to_remove;
                                                    filters_to_remove.reserve(selected_filters.size());
                                                    for (const auto& filter : selected_filters)
                                                    {
                                                        filters_to_remove.push_back(AllProcessedSources::FilterSelection {filter.include, filter.filter_index, filter.label});
                                                    }

                                                    if (!_context.processed_sources.remove_filters(filters_to_remove))
                                                    {
                                                        return {false, "Failed to delete selected filters", false};
                                                    }

                                                    _context.log_view.rebuild_view(_context.processed_sources);
                                                    return {true, "Deleted " + std::to_string(filters_to_remove.size()) + " filter" + (filters_to_remove.size() == 1 ? "" : "s")};
                                                });

    return {true, "Mark filters to delete and press Enter", false};
}

} // namespace slayerlog
