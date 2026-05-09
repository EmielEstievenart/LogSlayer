#pragma once

#include <optional>
#include <string>
#include <vector>

#include "command.hpp"
#include "command_context.hpp"
#include "command_widgets/single_select_list.hpp"

namespace slayerlog
{

class CloseOpenFileCommand final : public Command
{
public:
    explicit CloseOpenFileCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;
    bool has_active_interaction() const override;
    CommandEventResult handle_event(const ftxui::Event& event) override;
    ftxui::Element render() override;
    std::string palette_title() const override;
    ftxui::Element render_help() const override;
    void cancel() override;

private:
    CommandContext _context;
    bool _active = false;
    std::vector<std::string> _labels;
    std::optional<SingleSelectList> _picker;
};

} // namespace slayerlog
