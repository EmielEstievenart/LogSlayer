#pragma once

#include <optional>
#include <string>
#include <vector>

#include "command.hpp"
#include "command_context.hpp"
#include "command_widgets/single_select_list.hpp"

namespace slayerlog
{

/// Interactive command that opens the LogView2 dual-pane "align time" mode. It first
/// shows a source picker; confirming a source hands off to LogViewService::begin_time_alignment,
/// which drives the side-by-side panes and the nudge workflow. (Use adjust-time-offset /
/// clear-time-offset for manual offset entry and resetting.)
class AlignTimeViewCommand final : public Command
{
public:
    explicit AlignTimeViewCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
    bool has_active_interaction() const override;
    CommandEventResult handle_event(const ftxui::Event& event) override;
    ftxui::Element render() override;
    std::string palette_title() const override;
    ftxui::Element render_help() const override;
    void cancel() override;

private:
    void reset();

    CommandContext _context;
    bool _active = false;
    std::vector<std::string> _labels;
    std::vector<std::string> _picker_labels;
    std::optional<SingleSelectList> _source_picker;
};

} // namespace slayerlog
