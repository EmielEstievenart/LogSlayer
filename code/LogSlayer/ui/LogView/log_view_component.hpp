#pragma once

#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <ftxui_components/text_view_component.hpp>
#include <ftxui_components/text_view_controller.hpp>

#include "command_palette_controller.hpp"
#include "log_view_data.hpp"
#include "log_view_find_manager.hpp"
#include "log_view_selection.hpp"

namespace slayerlog
{

// The primary log view as a native FTXUI component: a thin pull-based renderer
// over a LogViewData, with its own find manager, selection, and command palette.
class LogViewComponent : public ftxui::ComponentBase
{
public:
    LogViewComponent(std::string title, std::shared_ptr<LogViewData> data, CommandPaletteController& command_palette_controller, std::function<void()> on_exit = {});

    // The find manager owned by this view, exposed so the command palette can
    // drive the same search state the view renders.
    [[nodiscard]] LogViewFindManager& find_manager();

    // The text view controller backing this view, exposed so a LogViewService
    // implementation can drive navigation (centring, viewport queries).
    [[nodiscard]] TextViewController& text_view_controller();

private:
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    bool handle_mouse(const ftxui::Mouse& mouse);
    bool handle_left_button(const ftxui::Mouse& mouse);
    void copy_selection();

    std::string _title;
    std::shared_ptr<LogViewData> _data;
    LogViewFindManager _find_manager;
    CommandPaletteController& _command_palette_controller;
    std::function<void()> _on_exit;
    std::shared_ptr<TextViewComponent> _text_view;
    LogViewSelection _selection;
    ftxui::Box _box;
};

} // namespace slayerlog
