#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

#include "command_palette_controller.hpp"
#include "command_palette_view.hpp"
#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "log_view.hpp"

namespace slayerlog
{

class MasterView
{
public:
    MasterView(LogView& log_view, CommandPaletteView& command_palette_view);

    ftxui::Element render(const AllProcessedSources& processed_sources, LogController& controller, const std::string& header_text, int screen_height,
                          CommandPaletteController& command_palette_controller);

private:
    LogView& _log_view;
    CommandPaletteView& _command_palette_view;
};

} // namespace slayerlog
