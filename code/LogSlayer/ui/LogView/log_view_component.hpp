#pragma once

#include <mutex>
#include <string>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "command_palette_controller.hpp"
#include "log_controller.hpp"
#include "log_view.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

// The left-hand log view as a native FTXUI component. It owns the command
// palette: Ctrl+P/F/R are intercepted here, so they only fire while this view
// is focused. All rendering and scroll/find/selection handling is delegated to
// the existing LogView helper.
class LogViewComponent : public ftxui::ComponentBase
{
public:
    LogViewComponent(AllProcessedSources& processed_sources, LogController& controller, CommandPaletteController& command_palette_controller, ftxui::ScreenInteractive& screen, const std::string& header_text,
                     std::mutex& model_mutex);

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    bool exit_requested() const;

private:
    AllProcessedSources& _processed_sources;
    LogController& _controller;
    CommandPaletteController& _command_palette_controller;
    ftxui::ScreenInteractive& _screen;
    const std::string& _header_text;
    std::mutex& _model_mutex;
    LogView _log_view;
    ftxui::Box _box;
    bool _exit_requested = false;
};

} // namespace slayerlog
