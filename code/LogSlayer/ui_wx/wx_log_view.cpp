#include "wx_log_view.hpp"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/window.h>

namespace slayerlog
{

namespace
{

// Placeholder M1 palette until view_theme gains a toolkit-neutral core
// counterpart (docs/wx-ui-plan.md, M5).
const wxColour view_background_colour(30, 30, 30);
const wxColour view_text_colour(220, 220, 220);

} // namespace

WxLogView::WxLogView(wxWindow* parent, std::shared_ptr<LogView2Data> data) : wxScrolled<wxWindow>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxWANTS_CHARS), _data(std::move(data))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(view_background_colour);
    _font = wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));

    {
        wxClientDC dc(this);
        dc.SetFont(_font);
        _line_height = std::max(1, dc.GetCharHeight());
        _char_width  = std::max(1, dc.GetCharWidth());
    }

    SetScrollRate(_char_width, _line_height);
    Bind(wxEVT_PAINT, &WxLogView::on_paint, this);
    Bind(wxEVT_KEY_DOWN, &WxLogView::on_key_down, this);

    on_model_updated();
}

void WxLogView::on_model_updated()
{
    const bool stick_to_bottom = is_scrolled_to_bottom();

    {
        auto lock          = _data->lock();
        _total_rows        = _data->size();
        _widest_line_width = _data->widest_line_width();
    }

    SetVirtualSize((_widest_line_width + 1) * _char_width, static_cast<int>(_total_rows) * _line_height);
    if (stick_to_bottom)
    {
        scroll_to_bottom();
    }

    Refresh();
}

void WxLogView::on_paint(wxPaintEvent& /*event*/)
{
    wxAutoBufferedPaintDC dc(this);
    DoPrepareDC(dc);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    dc.SetFont(_font);
    dc.SetTextForeground(view_text_colour);

    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);

    auto lock                 = _data->lock();
    const std::size_t total   = _data->size();
    const auto first_row      = static_cast<std::size_t>(std::max(0, view_start_rows));
    const auto rows_that_fit  = static_cast<std::size_t>(visible_row_count() + 2);
    const std::size_t end_row = std::min(total, first_row + rows_that_fit);
    for (std::size_t row = first_row; row < end_row; ++row)
    {
        dc.DrawText(wxString::FromUTF8(_data->to_string(row)), 0, static_cast<int>(row) * _line_height);
    }
}

void WxLogView::on_key_down(wxKeyEvent& event)
{
    switch (event.GetKeyCode())
    {
    case 'Q':
    case WXK_ESCAPE:
        wxGetTopLevelParent(this)->Close();
        return;
    case WXK_END:
        scroll_to_bottom();
        Refresh();
        return;
    default:
        event.Skip();
        return;
    }
}

bool WxLogView::is_scrolled_to_bottom() const
{
    if (_total_rows == 0)
    {
        return true;
    }

    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);
    return view_start_rows + visible_row_count() >= static_cast<int>(_total_rows);
}

int WxLogView::visible_row_count() const
{
    return std::max(1, GetClientSize().GetHeight() / _line_height);
}

void WxLogView::scroll_to_bottom()
{
    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);
    Scroll(view_start_columns, std::max(0, static_cast<int>(_total_rows) - visible_row_count()));
}

} // namespace slayerlog
