#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

namespace slayerlog
{

// The right-hand log view as a native FTXUI component. It has no behavior yet:
// it only draws a bordered panel that takes focus (via the container) and
// swallows input while focused. Functionality will be added incrementally.
class LogView2Component : public ftxui::ComponentBase
{
public:
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

private:
    ftxui::Box _box;
};

} // namespace slayerlog
