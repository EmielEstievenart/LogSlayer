#include "wx_log_view.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include <wx/dcbuffer.h>
#include <wx/window.h>

namespace slayerlog
{

namespace
{

// Placeholder dark palette until view_theme gains a toolkit-neutral core
// counterpart (docs/wx-ui-plan.md, M5).
const wxColour view_background_colour(30, 30, 30);
const wxColour view_text_colour(220, 220, 220);
const wxColour find_match_background_colour(28, 72, 128);
const wxColour find_match_text_colour(235, 235, 235);
const wxColour find_active_background_colour(190, 150, 0);
const wxColour find_active_text_colour(20, 20, 20);

} // namespace

WxLogView::WxLogView(wxWindow* parent, std::shared_ptr<LogView2Data> data) : wxScrolled<wxWindow>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxWANTS_CHARS), _data(std::move(data)), _find_manager(_data)
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
    Bind(wxEVT_SIZE, &WxLogView::on_size, this);

    on_model_updated();
}

void WxLogView::on_model_updated()
{
    std::optional<std::size_t> focus_row;
    {
        auto lock          = _data->lock();
        _total_rows        = _data->size();
        _widest_line_width = _data->widest_line_width();
        focus_row          = _find_manager.consume_pending_focus_line();
    }

    // Clamp in 64-bit: wx virtual sizes are ints, and rows * line height
    // overflows for huge logs (rows past the clamp need row-unit scrolling,
    // planned with the M5 view parity work).
    const auto clamp_to_int = [](long long value) { return static_cast<int>(std::min<long long>(value, std::numeric_limits<int>::max())); };
    SetVirtualSize(clamp_to_int((static_cast<long long>(_widest_line_width) + 1) * _char_width), clamp_to_int(static_cast<long long>(_total_rows) * _line_height));

    if (_pending_center_row.has_value())
    {
        center_on_row(*std::exchange(_pending_center_row, std::nullopt));
    }
    else if (focus_row.has_value())
    {
        center_on_row(*focus_row);
    }
    else if (_follow_tail)
    {
        scroll_to_bottom();
    }

    Refresh();
}

void WxLogView::request_center_on_row(std::size_t row)
{
    _pending_center_row = row;
}

LogView2FindManager& WxLogView::find_manager()
{
    return _find_manager;
}

void WxLogView::center_on_row(std::size_t row)
{
    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);
    Scroll(view_start_columns, std::max(0, static_cast<int>(std::min<std::size_t>(row, static_cast<std::size_t>(std::numeric_limits<int>::max()))) - visible_row_count() / 2));
    // Recompute follow immediately: a watcher redraw before the next paint
    // must not scroll_to_bottom over the row we just centered.
    _follow_tail = is_scrolled_to_bottom();
    Refresh();
}

int WxLogView::first_visible_row() const
{
    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);
    return std::max(0, view_start_rows);
}

int WxLogView::viewport_row_count() const
{
    return visible_row_count();
}

void WxLogView::set_palette_callbacks(std::function<void()> open_commands, std::function<void()> open_find, std::function<void()> open_history)
{
    _open_commands = std::move(open_commands);
    _open_find     = std::move(open_find);
    _open_history  = std::move(open_history);
}

void WxLogView::on_paint(wxPaintEvent& /*event*/)
{
    wxAutoBufferedPaintDC dc(this);
    DoPrepareDC(dc);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    dc.SetFont(_font);
    dc.SetPen(*wxTRANSPARENT_PEN);

    int view_start_columns = 0;
    int view_start_rows    = 0;
    GetViewStart(&view_start_columns, &view_start_rows);

    // The DC origin is shifted by DoPrepareDC, so the highlight strip has to
    // start at the logical position of the client area's left edge.
    const int highlight_left  = CalcUnscrolledPosition(wxPoint(0, 0)).x;
    const int highlight_width = GetClientSize().GetWidth();

    auto lock                 = _data->lock();
    const std::size_t total   = _data->size();
    const auto first_row      = static_cast<std::size_t>(std::max(0, view_start_rows));
    const auto rows_that_fit  = static_cast<std::size_t>(visible_row_count() + 2);
    const std::size_t end_row = std::min(total, first_row + rows_that_fit);
    for (std::size_t row = first_row; row < end_row; ++row)
    {
        const std::string text = _data->to_string(row);
        wxString line          = wxString::FromUTF8(text.data(), text.size());
        if (line.empty() && !text.empty())
        {
            // Raw log bytes are not always valid UTF-8 (FromUTF8 rejects the
            // whole line then); degrade to Latin-1 rather than a blank row.
            line = wxString::From8BitData(text.data(), text.size());
        }

        if (_find_manager.line_matches(row))
        {
            const bool is_active_match = _find_manager.line_is_active_match(row);
            dc.SetBrush(wxBrush(is_active_match ? find_active_background_colour : find_match_background_colour));
            dc.DrawRectangle(highlight_left, static_cast<int>(row) * _line_height, highlight_width, _line_height);
            dc.SetTextForeground(is_active_match ? find_active_text_colour : find_match_text_colour);
        }
        else
        {
            dc.SetTextForeground(view_text_colour);
        }

        dc.DrawText(line, 0, static_cast<int>(row) * _line_height);
    }

    // Follow state derives from where the user actually is after any scroll
    // (user or programmatic): at the bottom means attached.
    _follow_tail = is_scrolled_to_bottom();
}

void WxLogView::on_key_down(wxKeyEvent& event)
{
    if (event.ControlDown() && !event.AltDown() && !event.ShiftDown())
    {
        switch (event.GetKeyCode())
        {
        case 'P':
            if (_open_commands)
            {
                _open_commands();
                return;
            }
            break;
        case 'F':
            if (_open_find)
            {
                _open_find();
                return;
            }
            break;
        case 'R':
            if (_open_history)
            {
                _open_history();
                return;
            }
            break;
        default:
            break;
        }
    }

    switch (event.GetKeyCode())
    {
    case 'Q':
        wxGetTopLevelParent(this)->Close();
        return;
    case WXK_ESCAPE:
    {
        bool cleared_find = false;
        {
            auto lock = _data->lock();
            if (_find_manager.active())
            {
                _find_manager.clear();
                cleared_find = true;
            }
        }

        if (cleared_find)
        {
            Refresh();
            return;
        }

        wxGetTopLevelParent(this)->Close();
        return;
    }
    case WXK_LEFT:
    case WXK_RIGHT:
        if (navigate_find_matches(event.GetKeyCode() == WXK_RIGHT))
        {
            return;
        }

        event.Skip();
        return;
    case WXK_END:
        _follow_tail = true;
        scroll_to_bottom();
        Refresh();
        return;
    default:
        event.Skip();
        return;
    }
}

bool WxLogView::navigate_find_matches(bool forward)
{
    bool find_active = false;
    std::optional<std::size_t> focus_row;
    {
        auto lock = _data->lock();
        if (_find_manager.active())
        {
            find_active = true;
            if (forward)
            {
                _find_manager.go_to_next_match();
            }
            else
            {
                _find_manager.go_to_previous_match();
            }

            focus_row = _find_manager.consume_pending_focus_line();
        }
    }

    if (!find_active)
    {
        return false;
    }

    if (focus_row.has_value())
    {
        center_on_row(*focus_row);
    }

    Refresh();
    return true;
}

void WxLogView::on_size(wxSizeEvent& event)
{
    // A shrink would otherwise silently detach the tail (the bottom rows fall
    // out of view before the next model update re-evaluates follow).
    if (_follow_tail)
    {
        scroll_to_bottom();
    }

    event.Skip();
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
