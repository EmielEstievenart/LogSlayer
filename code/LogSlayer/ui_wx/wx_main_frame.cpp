#include "wx_main_frame.hpp"

#include <utility>

#include <wx/sizer.h>

#include "wx_command_palette.hpp"
#include "wx_log_view.hpp"

namespace slayerlog
{

WxMainFrame::WxMainFrame(const wxString& title, std::shared_ptr<LogView2Data> data) : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1100, 700))
{
    _log_view = new WxLogView(this, std::move(data));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(_log_view, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_CLOSE_WINDOW, &WxMainFrame::handle_close, this);
    Bind(wxEVT_SIZE, &WxMainFrame::handle_size, this);
    _log_view->SetFocus();
}

void WxMainFrame::set_on_close(std::function<void()> on_close)
{
    _on_close = std::move(on_close);
}

void WxMainFrame::on_model_updated()
{
    if (_log_view != nullptr)
    {
        _log_view->on_model_updated();
    }
}

WxLogView& WxMainFrame::log_view()
{
    return *_log_view;
}

void WxMainFrame::attach_command_palette(CommandPaletteSession& session, std::mutex& model_mutex)
{
    _command_palette = new WxCommandPalette(this, session, model_mutex);
    _command_palette->set_on_closed(
        [this]
        {
            if (_log_view != nullptr)
            {
                _log_view->SetFocus();
            }
        });
    _command_palette->set_on_command_executed(
        [this]
        {
            if (_log_view != nullptr)
            {
                _log_view->on_model_updated();
            }
        });
    _log_view->set_palette_callbacks([this] { _command_palette->open_commands(); }, [this] { _command_palette->open_with_query("find "); }, [this] { _command_palette->open_history(); });
}

void WxMainFrame::handle_close(wxCloseEvent& event)
{
    if (_on_close)
    {
        auto on_close = std::exchange(_on_close, nullptr);
        on_close();
    }

    event.Skip();
}

void WxMainFrame::handle_size(wxSizeEvent& event)
{
    if (_command_palette != nullptr)
    {
        _command_palette->reposition();
    }

    event.Skip();
}

} // namespace slayerlog
