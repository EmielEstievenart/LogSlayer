#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include <wx/frame.h>

#include "log_view2_data.hpp"

namespace slayerlog
{

class CommandPaletteSession;
class WxCommandPalette;
class WxLogView;

/// Top-level GUI frame hosting the log view and the command palette overlay.
/// The composition root registers an on_close hook that stops the watcher
/// thread before the frame (and the view the redraw scheduler targets) is
/// destroyed.
class WxMainFrame : public wxFrame
{
public:
    WxMainFrame(const wxString& title, std::shared_ptr<LogView2Data> data);

    void set_on_close(std::function<void()> on_close);

    /// GUI-thread entry point the redraw scheduler drives.
    void on_model_updated();

    [[nodiscard]] WxLogView& log_view();

    /// Creates the palette overlay over the shared core session and wires the
    /// log view's Ctrl+P / Ctrl+F / Ctrl+R shortcuts to it. Called by the
    /// composition root once the command manager is registered.
    void attach_command_palette(CommandPaletteSession& session, std::mutex& model_mutex);

private:
    void handle_close(wxCloseEvent& event);
    void handle_size(wxSizeEvent& event);

    WxLogView* _log_view               = nullptr;
    WxCommandPalette* _command_palette = nullptr;
    std::function<void()> _on_close;
};

} // namespace slayerlog
