#include "implementations/log_view1/delete_filters_command.hpp"

#include <cctype>
#include <string>
#include <utility>

#include "log_view_service.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "view_theme.hpp"

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

DeleteFiltersCommand::DeleteFiltersCommand(CommandContext context) : _context(context) { }

const CommandDescriptor& DeleteFiltersCommand::descriptor() const
{
    static const CommandDescriptor descriptor {"delete-filters", "Delete one or more active filters", "delete-filters", {"Open a picker containing every active filter.", "Use Up/Down to select a filter, Space to mark it, and Enter to delete the marked filters.", "Each filter is labeled with (in) or (out)."}};
    return descriptor;
}

CommandResult DeleteFiltersCommand::execute(std::string_view arguments)
{
    if (!trim_text(arguments).empty())
    {
        return {false, "Usage: delete-filters"};
    }

    _candidates.clear();
    std::vector<std::string> labels;
    for (const auto& filter : _context.processed_sources.all_filters())
    {
        _candidates.push_back({filter.text, filter.include, filter.index});
        labels.push_back(std::string(filter.include ? "(in) " : "(out) ") + filter.text);
    }

    if (_candidates.empty())
    {
        return {false, "No filters to delete"};
    }

    _picker.emplace(std::move(labels));
    _active = true;
    return {true, "Mark filters to delete and press Enter", false};
}

bool DeleteFiltersCommand::has_active_interaction() const
{
    return _active;
}

CommandEventResult DeleteFiltersCommand::handle_event(const ftxui::Event& event)
{
    if (!_active || !_picker.has_value())
    {
        return {};
    }

    if (event == ftxui::Event::Escape)
    {
        cancel();
        return {true, std::nullopt};
    }

    if (event == ftxui::Event::Return)
    {
        std::vector<AllProcessedSources::FilterSelection> filters_to_remove;
        for (const std::size_t index : _picker->selected_indices())
        {
            if (index < _candidates.size())
            {
                const auto& candidate = _candidates[index];
                filters_to_remove.push_back({candidate.include, candidate.filter_index, candidate.label});
            }
        }

        if (filters_to_remove.empty())
        {
            return {true, CommandResult {false, "No filters are marked for deletion.", false}};
        }

        if (!_context.processed_sources.remove_filters(filters_to_remove))
        {
            return {true, CommandResult {false, "Failed to delete selected filters", false}};
        }

        _context.log_view.rebuild_view(_context.processed_sources);
        _active = false;
        return {true, CommandResult {true, "Deleted " + std::to_string(filters_to_remove.size()) + " filter" + (filters_to_remove.size() == 1 ? "" : "s")}};
    }

    return {_picker->handle_event(event), std::nullopt};
}

ftxui::Element DeleteFiltersCommand::render()
{
    if (!_picker.has_value())
    {
        return ftxui::emptyElement();
    }

    return ftxui::vbox({ftxui::text("Mark filters to delete") | ftxui::color(theme::muted), ftxui::separator(), _picker->render() | ftxui::flex});
}

std::string DeleteFiltersCommand::palette_title() const
{
    return "Delete Filters";
}

ftxui::Element DeleteFiltersCommand::render_help() const
{
    auto sep = []() { return ftxui::text("  "); };
    return ftxui::hbox({theme::key_hint("Up/Down", "select"), sep(), theme::key_hint("Space", "toggle"), sep(), theme::key_hint("Enter", "delete marked"), sep(), theme::key_hint("Esc", "cancel")});
}

void DeleteFiltersCommand::cancel()
{
    _active = false;
    _picker.reset();
    _candidates.clear();
}

} // namespace slayerlog
