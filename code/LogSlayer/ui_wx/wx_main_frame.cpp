#include "wx_main_frame.hpp"

#include <utility>

#include <wx/sizer.h>

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

void WxMainFrame::handle_close(wxCloseEvent& event)
{
    if (_on_close)
    {
        auto on_close = std::exchange(_on_close, nullptr);
        on_close();
    }

    event.Skip();
}

} // namespace slayerlog
