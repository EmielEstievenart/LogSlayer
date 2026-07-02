#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <wx/font.h>
#include <wx/panel.h>

#include "command_palette_result_lines.hpp"
#include "command_palette_session.hpp"

namespace slayerlog
{

/// Custom-drawn command palette overlay for the wx UI: a centered-top child
/// panel of the main frame, shown while the shared core CommandPaletteSession
/// is open. Every key is translated into a session call (the session owns all
/// palette behavior; this class only draws the model and mirrors the session's
/// result list via the two session callbacks), and every session mutation runs
/// under the model mutex, exactly like the TUI's event path. The interactive
/// picker modes are unreachable in the wx UI until their commands are
/// registered (docs/wx-ui-plan.md, M4).
class WxCommandPalette : public wxPanel
{
public:
    WxCommandPalette(wxWindow* parent, CommandPaletteSession& session, std::mutex& model_mutex);
    ~WxCommandPalette() override;

    void open_commands();
    void open_with_query(std::string query);
    void open_history();
    [[nodiscard]] bool is_open() const;

    /// Re-center and re-size against the parent's client area; the frame calls
    /// this from its size handler.
    void reposition();

    /// Invoked after the palette closes so the composition can restore focus
    /// to the log view.
    void set_on_closed(std::function<void()> on_closed);

    /// Invoked after Enter ran a command, so the composition can resync the
    /// log view (find focus lines, sizes) outside the model lock.
    void set_on_command_executed(std::function<void()> on_command_executed);

private:
    void on_paint(wxPaintEvent& event);
    void on_key_down(wxKeyEvent& event);
    void on_char(wxKeyEvent& event);
    void handle_results_changed();
    void handle_selection_changed();
    void sync_after_session_change();
    void ensure_selected_visible();
    [[nodiscard]] std::optional<std::pair<int, int>> selected_line_range() const;
    [[nodiscard]] bool results_selectable() const;
    [[nodiscard]] int visible_result_line_count() const;
    [[nodiscard]] int selection_page_entry_count() const;
    [[nodiscard]] int desired_height() const;

    template <typename Action>
    void with_model_lock(Action&& action)
    {
        std::lock_guard<std::mutex> lock(_model_mutex);
        action();
    }

    CommandPaletteSession& _session;
    std::mutex& _model_mutex;
    std::function<void()> _on_closed;
    std::function<void()> _on_command_executed;
    CommandPaletteResultLines _result_list;
    int _first_visible_line = 0;
    wxFont _font;
    int _line_height = 1;
    int _char_width  = 1;
};

} // namespace slayerlog
