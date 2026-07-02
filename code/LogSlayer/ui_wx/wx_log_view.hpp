#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

#include <wx/font.h>
#include <wx/scrolwin.h>

#include "log_view2_data.hpp"
#include "log_view2_find_manager.hpp"

namespace slayerlog
{

/// Custom-drawn, pull-based log view: the wx counterpart of the FTXUI
/// LogView2Component's canvas. Each paint pulls only the visible rows straight
/// from the LogView2Data under its lock, so rendering stays O(viewport)
/// however large the log grows. Tail-follow is a persistent flag: it is
/// recomputed from geometry after every paint (scrolling away from the bottom
/// detaches, reaching the bottom re-attaches), End re-attaches explicitly, and
/// resizes re-pin while attached. The view owns the LogView2FindManager: rows
/// matching the active find query get a highlight background (the active match
/// a distinct one), Left/Right step between matches while a find is active,
/// and Esc clears the find before it closes the window.
class WxLogView : public wxScrolled<wxWindow>
{
public:
    WxLogView(wxWindow* parent, std::shared_ptr<LogView2Data> data);

    /// Recompute the virtual size after the model changed and repaint, keeping
    /// the view pinned to the tail while follow is attached (a pending find
    /// focus line wins over the tail and is centered instead). Must be called
    /// on the GUI thread (the WxRedrawScheduler marshals onto it).
    void on_model_updated();

    [[nodiscard]] LogView2FindManager& find_manager();

    /// Scroll so the given row sits in the middle of the viewport.
    void center_on_row(std::size_t row);

    /// Record a row to center on during the next on_model_updated. Safe to
    /// call while model_mutex is held (centering immediately would force a
    /// repaint that re-locks it).
    void request_center_on_row(std::size_t row);
    [[nodiscard]] int first_visible_row() const;
    [[nodiscard]] int viewport_row_count() const;

    /// Palette shortcuts routed to the composition root: Ctrl+P opens the
    /// command palette, Ctrl+F opens it pre-filled with "find ", Ctrl+R opens
    /// the command history.
    void set_palette_callbacks(std::function<void()> open_commands, std::function<void()> open_find, std::function<void()> open_history);

private:
    void on_paint(wxPaintEvent& event);
    void on_key_down(wxKeyEvent& event);
    void on_size(wxSizeEvent& event);
    bool navigate_find_matches(bool forward);
    [[nodiscard]] bool is_scrolled_to_bottom() const;
    [[nodiscard]] int visible_row_count() const;
    void scroll_to_bottom();

    std::shared_ptr<LogView2Data> _data;
    LogView2FindManager _find_manager;
    std::optional<std::size_t> _pending_center_row;
    std::function<void()> _open_commands;
    std::function<void()> _open_find;
    std::function<void()> _open_history;
    wxFont _font;
    int _line_height        = 1;
    int _char_width         = 1;
    std::size_t _total_rows = 0;
    int _widest_line_width  = 0;
    bool _follow_tail       = true;
};

} // namespace slayerlog
