#pragma once

#include <ftxui/dom/elements.hpp>

#include "command_palette_controller.hpp"

namespace slayerlog
{

class CommandPaletteView
{
public:
    ftxui::Element render(CommandPaletteController& command_palette_controller, int screen_height);
};

} // namespace slayerlog
