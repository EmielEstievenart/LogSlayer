#pragma once

#include <ftxui/dom/elements.hpp>

#include <ftxui_components/text_view_view.hpp>

#include "command_palette_controller.hpp"

namespace slayerlog
{

class CommandPaletteView
{
public:
    ftxui::Element render(CommandPaletteController& command_palette_controller, int preferred_result_height);

private:
    TextViewView _result_text_view;
};

} // namespace slayerlog
