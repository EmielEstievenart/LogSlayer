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

    const int log_viewport_height       = controller.text_view_controller().viewport_line_count();
    const int palette_viewport_height   = (log_viewport_height * 2) / 3;
    const int preferred_palette_height  = palette_viewport_height > 0 ? palette_viewport_height : 1;

    return ftxui::dbox({
        std::move(base_view),
        _command_palette_view.render(command_palette_controller, preferred_palette_height),
    });
}

} // namespace slayerlog
