#include "master_view.hpp"

namespace slayerlog
{

MasterView::MasterView(LogView& log_view, CommandPaletteView& command_palette_view) : _log_view(log_view), _command_palette_view(command_palette_view)
{
}

ftxui::Element MasterView::render(const AllProcessedSources& processed_sources, LogController& controller, const std::string& header_text, int screen_height,
                                  CommandPaletteController& command_palette_controller)
{
    const CommandPaletteModel& command_palette = command_palette_controller.model();
    auto base_view                             = _log_view.render(processed_sources, controller, header_text, screen_height, command_palette.hidden_column_preview);
    if (!command_palette_controller.is_open())
    {
        return base_view;
    }

    return ftxui::dbox({
        std::move(base_view),
        _command_palette_view.render(command_palette_controller, screen_height),
    });
}

} // namespace slayerlog
