#pragma once

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "redraw_scheduler.hpp"

namespace slayerlog
{

/// FTXUI RedrawScheduler: posts the custom event the pull-based views redraw
/// on. ScreenInteractive::PostEvent is thread-safe and FTXUI coalesces
/// pending redraws, so this can be called from the watcher thread directly.
class FtxuiRedrawScheduler final : public RedrawScheduler
{
public:
    explicit FtxuiRedrawScheduler(ftxui::ScreenInteractive& screen) : _screen(screen) { }

    void request_redraw() override { _screen.PostEvent(ftxui::Event::Custom); }

private:
    ftxui::ScreenInteractive& _screen;
};

} // namespace slayerlog
