#pragma once

#include <optional>
#include <string>
#include <vector>

#include "command.hpp"
#include "command_context.hpp"
#include "command_widgets/single_select_list.hpp"
#include "command_widgets/text_input_panel.hpp"

namespace slayerlog
{

class SetTimeOffsetCommand final : public Command
{
public:
    explicit SetTimeOffsetCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
    bool has_active_interaction() const override;
    CommandEventResult handle_event(const ftxui::Event& event) override;
    ftxui::Element render() override;
    std::string palette_title() const override;
    ftxui::Element render_help() const override;
    void cancel() override;

private:
    enum class State { Inactive, SourceSelection, OffsetInput };
    void reset();
    void refresh_preview();

    CommandContext _context;
    State _state = State::Inactive;
    std::vector<std::string> _labels;
    std::optional<std::size_t> _selected_source_index;
    std::optional<SingleSelectList> _source_picker;
    std::optional<TextInputPanel> _input;
};

} // namespace slayerlog
