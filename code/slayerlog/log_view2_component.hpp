#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <ftxui_components/text_view_component.hpp>

namespace slayerlog
{

// The right-hand log view as a native FTXUI component. It currently hosts a
// placeholder text view while functionality is added incrementally.
class LogView2Component : public ftxui::ComponentBase
{
public:
    LogView2Component();

private:
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    std::vector<std::string> _lines;
    std::shared_ptr<TextViewComponent> _text_view;
    ftxui::Box _box;
};

} // namespace slayerlog
