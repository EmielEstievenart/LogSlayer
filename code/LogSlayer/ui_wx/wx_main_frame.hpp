#pragma once

#include <functional>
#include <memory>

#include <wx/frame.h>

#include "log_view2_data.hpp"

namespace slayerlog
{

class WxLogView;

/// Top-level GUI frame hosting the log view. The composition root registers
/// an on_close hook that stops the watcher thread before the frame (and the
/// view the redraw scheduler targets) is destroyed.
class WxMainFrame : public wxFrame
{
public:
    WxMainFrame(const wxString& title, std::shared_ptr<LogView2Data> data);

    void set_on_close(std::function<void()> on_close);

    /// GUI-thread entry point the redraw scheduler drives.
    void on_model_updated();

private:
    void handle_close(wxCloseEvent& event);

    WxLogView* _log_view = nullptr;
    std::function<void()> _on_close;
};

} // namespace slayerlog
