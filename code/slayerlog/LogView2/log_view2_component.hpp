#pragma once

#include <memory>
#include <string>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <ftxui_components/text_view_component.hpp>

#include "command_palette_controller.hpp"
#include "log_view2_data.hpp"
#include "log_view2_selection.hpp"

namespace slayerlog
{

// The right-hand log view as a native FTXUI component. It currently hosts a
// placeholder text view while functionality is added incrementally.
class LogView2Component : public ftxui::ComponentBase
{
public:
    LogView2Component(std::string title, std::shared_ptr<LogView2Data> data, CommandPaletteController& command_palette_controller);

private:
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    bool handle_mouse(const ftxui::Mouse& mouse);
    bool handle_left_button(const ftxui::Mouse& mouse);
    void copy_selection();

    std::string _title;
    std::shared_ptr<LogView2Data> _data;
    CommandPaletteController& _command_palette_controller;
    std::shared_ptr<TextViewComponent> _text_view;
    LogView2Selection _selection;
    ftxui::Box _box;
};

} // namespace slayerlog
