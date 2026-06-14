#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "command.hpp"
#include "command_context.hpp"
#include "command_widgets/multi_select_list.hpp"

namespace slayerlog
{

class DeleteFiltersCommand final : public Command
{
public:
    explicit DeleteFiltersCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
    bool has_active_interaction() const override;
    CommandEventResult handle_event(const ftxui::Event& event) override;
    ftxui::Element render() override;
    std::string palette_title() const override;
    ftxui::Element render_help() const override;
    void cancel() override;

private:
    struct FilterDeleteCandidate
    {
        std::string label;
        bool include = true;
        std::size_t filter_index = 0;
    };

    CommandContext _context;
    bool _active = false;
    std::vector<FilterDeleteCandidate> _candidates;
    std::optional<MultiSelectList> _picker;
};

} // namespace slayerlog
