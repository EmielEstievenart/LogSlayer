#pragma once

#include <atomic>
#include <functional>
#include <utility>

#include <wx/event.h>

#include "redraw_scheduler.hpp"

namespace slayerlog
{

/// Wx RedrawScheduler: marshals redraw requests from any thread (watcher
/// thread included) onto the GUI thread via CallAfter, coalescing bursts with
/// a pending flag. The target handler must outlive every thread that can call
/// request_redraw(); the composition root guarantees that by joining the
/// watcher thread before the frame is destroyed.
class WxRedrawScheduler final : public RedrawScheduler
{
public:
    WxRedrawScheduler(wxEvtHandler& handler, std::function<void()> on_redraw) : _handler(handler), _on_redraw(std::move(on_redraw)) { }

    void request_redraw() override
    {
        if (_pending.exchange(true))
        {
            return;
        }

        _handler.CallAfter(
            [this]
            {
                _pending = false;
                _on_redraw();
            });
    }

private:
    wxEvtHandler& _handler;
    std::function<void()> _on_redraw;
    std::atomic<bool> _pending {false};
};

} // namespace slayerlog
