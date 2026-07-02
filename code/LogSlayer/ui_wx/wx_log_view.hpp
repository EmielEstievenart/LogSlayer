#pragma once

#include <cstddef>
#include <memory>

#include <wx/font.h>
#include <wx/scrolwin.h>

#include "log_view2_data.hpp"

namespace slayerlog
{

/// Custom-drawn, pull-based log view: the wx counterpart of the FTXUI
/// LogView2Component's canvas. Each paint pulls only the visible rows straight
/// from the LogView2Data under its lock, so rendering stays O(viewport)
/// however large the log grows. The view sticks to the tail while scrolled to
/// the bottom; scrolling up detaches, End re-attaches.
class WxLogView : public wxScrolled<wxWindow>
{
public:
    WxLogView(wxWindow* parent, std::shared_ptr<LogView2Data> data);

    /// Recompute the virtual size after the model changed and repaint, keeping
    /// the view pinned to the tail when it was at the bottom. Must be called
    /// on the GUI thread (the WxRedrawScheduler marshals onto it).
    void on_model_updated();

private:
    void on_paint(wxPaintEvent& event);
    void on_key_down(wxKeyEvent& event);
    [[nodiscard]] bool is_scrolled_to_bottom() const;
    [[nodiscard]] int visible_row_count() const;
    void scroll_to_bottom();

    std::shared_ptr<LogView2Data> _data;
    wxFont _font;
    int _line_height        = 1;
    int _char_width         = 1;
    std::size_t _total_rows = 0;
    int _widest_line_width  = 0;
};

} // namespace slayerlog
