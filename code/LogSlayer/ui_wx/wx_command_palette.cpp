#include "wx_command_palette.hpp"

#include <algorithm>
#include <cstddef>

#include <wx/dcbuffer.h>
#include <wx/window.h>

namespace slayerlog
{

namespace
{

// Placeholder dark palette until view_theme gains a toolkit-neutral core
// counterpart (docs/wx-ui-plan.md, M5).
const wxColour palette_background_colour(45, 45, 48);
const wxColour palette_border_colour(100, 100, 100);
const wxColour palette_text_colour(220, 220, 220);
const wxColour palette_muted_colour(140, 140, 140);
const wxColour palette_selection_background_colour(14, 99, 156);
const wxColour palette_selection_text_colour(240, 240, 240);
const wxColour palette_error_colour(244, 135, 113);
const wxColour palette_success_colour(137, 209, 133);

constexpr int max_visible_result_lines = 14;
constexpr int panel_padding            = 8;
constexpr int section_gap              = 5;
constexpr int desired_width_in_columns = 120;

std::string palette_title(CommandPaletteMode mode)
{
    switch (mode)
    {
    case CommandPaletteMode::History:
        return "Command History";
    case CommandPaletteMode::CloseOpenFile:
        return "Close Open File";
    case CommandPaletteMode::SelectTimestampSource:
        return "Select Log Source";
    case CommandPaletteMode::SelectTimestampFormat:
        return "Select Timestamp Format";
    case CommandPaletteMode::EnterTimestampOffset:
        return "Set Timestamp Offset";
    case CommandPaletteMode::DeleteFilters:
        return "Delete Filters";
    case CommandPaletteMode::Commands:
        break;
    }

    return "Command Palette";
}

std::string palette_key_hints(CommandPaletteMode mode)
{
    if (mode == CommandPaletteMode::History)
    {
        return "Enter execute   Tab copy to input   Ctrl+R commands   Esc close";
    }

    if (mode == CommandPaletteMode::CloseOpenFile)
    {
        return "Up/Down select   Enter close file   Esc cancel";
    }

    if (mode == CommandPaletteMode::SelectTimestampSource || mode == CommandPaletteMode::SelectTimestampFormat)
    {
        return "Up/Down select   Enter confirm   Esc cancel";
    }

    if (mode == CommandPaletteMode::EnterTimestampOffset)
    {
        return "Enter apply   Esc cancel";
    }

    if (mode == CommandPaletteMode::DeleteFilters)
    {
        return "Up/Down select   Space toggle   Enter delete marked   Esc cancel";
    }

    return "Tab complete   Enter execute   Ctrl+R history   Esc close";
}

/// Sub-header shown instead of the query prompt in the list-picker modes,
/// matching the TUI palette; empty for the modes that edit a query.
std::string picker_mode_header(CommandPaletteMode mode)
{
    switch (mode)
    {
    case CommandPaletteMode::CloseOpenFile:
        return "Select file to close";
    case CommandPaletteMode::SelectTimestampSource:
        return "Select source to reparse";
    case CommandPaletteMode::SelectTimestampFormat:
        return "Select timestamp format";
    case CommandPaletteMode::DeleteFilters:
        return "Mark filters to delete";
    case CommandPaletteMode::Commands:
    case CommandPaletteMode::History:
    case CommandPaletteMode::EnterTimestampOffset:
        break;
    }

    return {};
}

bool is_list_picker_mode(CommandPaletteMode mode)
{
    return mode == CommandPaletteMode::CloseOpenFile || mode == CommandPaletteMode::SelectTimestampSource || mode == CommandPaletteMode::SelectTimestampFormat || mode == CommandPaletteMode::DeleteFilters;
}

} // namespace

WxCommandPalette::WxCommandPalette(wxWindow* parent, CommandPaletteSession& session, std::mutex& model_mutex)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE), _session(session), _model_mutex(model_mutex)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(palette_background_colour);
    _font = wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));

    {
        wxClientDC dc(this);
        dc.SetFont(_font);
        _line_height = std::max(1, dc.GetCharHeight());
        _char_width  = std::max(1, dc.GetCharWidth());
    }

    Bind(wxEVT_PAINT, &WxCommandPalette::on_paint, this);
    Bind(wxEVT_KEY_DOWN, &WxCommandPalette::on_key_down, this);
    Bind(wxEVT_CHAR, &WxCommandPalette::on_char, this);

    _session.set_results_changed_callback([this] { handle_results_changed(); });
    _session.set_selection_changed_callback([this] { handle_selection_changed(); });
    handle_results_changed();

    Hide();
}

WxCommandPalette::~WxCommandPalette()
{
    // The session (a composition-root object) outlives this widget; drop the
    // callbacks so it cannot call back into a destroyed panel.
    _session.set_results_changed_callback({});
    _session.set_selection_changed_callback({});
}

void WxCommandPalette::open_commands()
{
    with_model_lock([this] { _session.open(); });
    sync_after_session_change();
}

void WxCommandPalette::open_with_query(std::string query)
{
    with_model_lock([this, &query] { _session.open_with_query(std::move(query)); });
    sync_after_session_change();
}

void WxCommandPalette::open_history()
{
    with_model_lock([this] { _session.open_history(); });
    sync_after_session_change();
}

bool WxCommandPalette::is_open() const
{
    return _session.is_open();
}

void WxCommandPalette::reposition()
{
    wxWindow* parent = GetParent();
    if (parent == nullptr)
    {
        return;
    }

    const wxSize client_size = parent->GetClientSize();
    const int desired_width  = desired_width_in_columns * _char_width + 2 * panel_padding;
    const int width          = std::max(240, std::min(desired_width, client_size.GetWidth() - 32));
    const int height         = std::min(desired_height(), std::max(_line_height * 4, client_size.GetHeight() - 32));
    SetSize((client_size.GetWidth() - width) / 2, 16, width, height);
}

void WxCommandPalette::set_on_closed(std::function<void()> on_closed)
{
    _on_closed = std::move(on_closed);
}

void WxCommandPalette::set_on_command_executed(std::function<void()> on_command_executed)
{
    _on_command_executed = std::move(on_command_executed);
}

void WxCommandPalette::on_paint(wxPaintEvent& /*event*/)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    dc.SetFont(_font);

    const wxSize client_size = GetClientSize();
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(palette_border_colour));
    dc.DrawRectangle(0, 0, client_size.GetWidth(), client_size.GetHeight());

    const CommandPaletteModel& model = _session.model();
    const int text_left              = panel_padding;
    int y                            = panel_padding;

    // Title row.
    dc.SetTextForeground(palette_muted_colour);
    dc.DrawText(wxString::FromUTF8(palette_title(model.mode)), text_left, y);
    y += _line_height;

    // In the list-picker modes there is no query to edit: draw the picker's
    // sub-header instead of the prompt, matching the TUI palette.
    const std::string mode_header = picker_mode_header(model.mode);
    if (!mode_header.empty())
    {
        dc.SetTextForeground(palette_muted_colour);
        dc.DrawText(wxString::FromUTF8(mode_header), text_left, y);
        y += _line_height + section_gap;

        // Separator.
        dc.SetPen(wxPen(palette_border_colour));
        dc.DrawLine(text_left, y, client_size.GetWidth() - panel_padding, y);
        y += 1 + section_gap;

        draw_result_list_and_status(dc, client_size, y);
        return;
    }

    // Query row with a block cursor, matching the TUI's prompt.
    const wxString prompt = "> ";
    dc.SetTextForeground(palette_text_colour);
    dc.DrawText(prompt, text_left, y);
    const int query_left = text_left + dc.GetTextExtent(prompt).GetWidth();
    if (model.query.empty())
    {
        dc.SetTextForeground(palette_muted_colour);
        dc.DrawText("Enter command", query_left + _char_width, y);
        dc.SetBrush(wxBrush(palette_text_colour));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(query_left, y, _char_width, _line_height);
    }
    else
    {
        const std::size_t cursor_position = std::min(model.cursor_position, model.query.size());
        const wxString prefix             = wxString::FromUTF8(model.query.substr(0, cursor_position));
        const bool cursor_at_end          = cursor_position >= model.query.size();
        const std::string cursor_text     = cursor_at_end ? " " : model.query.substr(cursor_position, 1);
        const wxString cursor_string      = wxString::FromUTF8(cursor_text);
        const wxString suffix             = cursor_at_end ? wxString() : wxString::FromUTF8(model.query.substr(cursor_position + 1));

        int x = query_left;
        dc.SetTextForeground(palette_text_colour);
        dc.DrawText(prefix, x, y);
        x += dc.GetTextExtent(prefix).GetWidth();

        const int cursor_width = std::max(_char_width, dc.GetTextExtent(cursor_string).GetWidth());
        dc.SetBrush(wxBrush(palette_text_colour));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(x, y, cursor_width, _line_height);
        dc.SetTextForeground(palette_background_colour);
        dc.DrawText(cursor_string, x, y);
        x += cursor_width;

        dc.SetTextForeground(palette_text_colour);
        dc.DrawText(suffix, x, y);
    }
    y += _line_height + section_gap;

    // Separator.
    dc.SetPen(wxPen(palette_border_colour));
    dc.DrawLine(text_left, y, client_size.GetWidth() - panel_padding, y);
    y += 1 + section_gap;

    draw_result_list_and_status(dc, client_size, y);
}

void WxCommandPalette::draw_result_list_and_status(wxDC& dc, const wxSize& client_size, int y)
{
    const CommandPaletteModel& model = _session.model();
    const int text_left              = panel_padding;

    // Result list.
    const bool selectable    = results_selectable();
    const int selected_index = model.selected_index;
    const int line_count     = static_cast<int>(_result_list.lines.size());
    const int first_line     = std::clamp(_first_visible_line, 0, std::max(0, line_count - 1));
    const int last_line      = std::min(line_count, first_line + visible_result_line_count());
    for (int line_index = first_line; line_index < last_line; ++line_index)
    {
        const int entry_index = _result_list.entry_indices[static_cast<std::size_t>(line_index)];
        const bool selected   = selectable && entry_index >= 0 && entry_index == selected_index;
        if (selected)
        {
            dc.SetBrush(wxBrush(palette_selection_background_colour));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(text_left - 2, y, client_size.GetWidth() - 2 * panel_padding + 4, _line_height);
            dc.SetTextForeground(palette_selection_text_colour);
        }
        else
        {
            dc.SetTextForeground(entry_index < 0 ? palette_muted_colour : palette_text_colour);
        }

        dc.DrawText(wxString::FromUTF8(_result_list.lines[static_cast<std::size_t>(line_index)]), text_left, y);
        y += _line_height;
    }
    y += section_gap;

    // Separator.
    dc.SetPen(wxPen(palette_border_colour));
    dc.DrawLine(text_left, y, client_size.GetWidth() - panel_padding, y);
    y += 1 + section_gap;

    // Status row: the last command result, the live offset preview while
    // entering a timestamp offset, or the key hints.
    if (!model.status_message.empty())
    {
        dc.SetTextForeground(model.status_is_error ? palette_error_colour : palette_success_colour);
        dc.DrawText(wxString::FromUTF8(model.status_message), text_left, y);
    }
    else if (model.mode == CommandPaletteMode::EnterTimestampOffset && !model.timestamp_offset_preview.empty())
    {
        dc.SetTextForeground(model.timestamp_offset_preview_is_error ? palette_error_colour : palette_success_colour);
        dc.DrawText(wxString::FromUTF8(model.timestamp_offset_preview), text_left, y);
    }
    else
    {
        dc.SetTextForeground(palette_muted_colour);
        dc.DrawText(wxString::FromUTF8(palette_key_hints(model.mode)), text_left, y);
    }
}

void WxCommandPalette::on_key_down(wxKeyEvent& event)
{
    const int key_code = event.GetKeyCode();

    if (event.ControlDown() && !event.AltDown() && key_code == 'R')
    {
        with_model_lock([this] { _session.toggle_history_mode(); });
        Refresh();
        return;
    }

    // The list-picker modes have no query to edit: Space toggles a mark in the
    // delete-filters picker, and everything except selection movement,
    // Enter, and Escape is swallowed, matching the TUI palette.
    const CommandPaletteMode mode = _session.model().mode;
    if (mode == CommandPaletteMode::DeleteFilters && key_code == WXK_SPACE)
    {
        with_model_lock([this] { _session.toggle_selected_filter(); });
        Refresh();
        return;
    }

    if (is_list_picker_mode(mode) && key_code != WXK_RETURN && key_code != WXK_NUMPAD_ENTER && key_code != WXK_ESCAPE && key_code != WXK_UP && key_code != WXK_DOWN && key_code != WXK_PAGEUP && key_code != WXK_PAGEDOWN)
    {
        return;
    }

    bool session_may_have_closed = false;
    switch (key_code)
    {
    case WXK_ESCAPE:
        with_model_lock([this] { _session.close(); });
        session_may_have_closed = true;
        break;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
        with_model_lock([this] { _session.submit(); });
        session_may_have_closed = true;
        if (_on_command_executed)
        {
            _on_command_executed();
        }
        break;
    case WXK_TAB:
        with_model_lock(
            [this]
            {
                if (_session.model().mode == CommandPaletteMode::History)
                {
                    _session.copy_selected_history_entry_to_query();
                }
                else if (_session.model().mode == CommandPaletteMode::Commands)
                {
                    _session.autocomplete_selected_command();
                }
            });
        break;
    case WXK_UP:
        with_model_lock([this] { _session.move_selection(-1); });
        break;
    case WXK_DOWN:
        with_model_lock([this] { _session.move_selection(1); });
        break;
    case WXK_PAGEUP:
        with_model_lock([this] { _session.move_selection(-selection_page_entry_count()); });
        break;
    case WXK_PAGEDOWN:
        with_model_lock([this] { _session.move_selection(selection_page_entry_count()); });
        break;
    case WXK_LEFT:
        with_model_lock([this] { _session.move_cursor_left(); });
        break;
    case WXK_RIGHT:
        with_model_lock([this] { _session.move_cursor_right(); });
        break;
    case WXK_HOME:
        with_model_lock([this] { _session.move_cursor_to_start(); });
        break;
    case WXK_END:
        with_model_lock([this] { _session.move_cursor_to_end(); });
        break;
    case WXK_BACK:
        with_model_lock([this] { _session.erase_previous_character(); });
        break;
    case WXK_DELETE:
        with_model_lock([this] { _session.erase_next_character(); });
        break;
    default:
        event.Skip();
        return;
    }

    if (session_may_have_closed)
    {
        sync_after_session_change();
    }

    Refresh();
}

void WxCommandPalette::on_char(wxKeyEvent& event)
{
    const wxChar unicode_key = event.GetUnicodeKey();
    if (unicode_key < 32 || unicode_key == WXK_DELETE || event.ControlDown() || event.AltDown())
    {
        event.Skip();
        return;
    }

    const std::string utf8_text = wxString(unicode_key).utf8_string();
    with_model_lock([this, &utf8_text] { _session.insert_text(utf8_text); });
    Refresh();
}

void WxCommandPalette::handle_results_changed()
{
    // May fire while the caller holds the model mutex (session callbacks run
    // inside the mutation): only read the palette model and repaint here.
    _result_list        = build_command_palette_result_lines(_session.model());
    _first_visible_line = 0;
    ensure_selected_visible();
    if (IsShown())
    {
        reposition();
    }

    Refresh();
}

void WxCommandPalette::handle_selection_changed()
{
    ensure_selected_visible();
    Refresh();
}

void WxCommandPalette::sync_after_session_change()
{
    if (_session.is_open())
    {
        reposition();
        if (!IsShown())
        {
            Show();
            Raise();
        }

        SetFocus();
        Refresh();
        return;
    }

    if (IsShown())
    {
        Hide();
        if (_on_closed)
        {
            _on_closed();
        }
    }
}

void WxCommandPalette::ensure_selected_visible()
{
    const auto selected_range = selected_line_range();
    if (!selected_range.has_value())
    {
        return;
    }

    const int visible_lines  = visible_result_line_count();
    const int selected_first = selected_range->first;
    const int selected_last  = selected_range->second - 1;

    if (selected_first < _first_visible_line)
    {
        _first_visible_line = selected_first;
    }
    else if (selected_last > _first_visible_line + visible_lines - 1)
    {
        _first_visible_line = selected_last - visible_lines + 1;
    }

    _first_visible_line = std::clamp(_first_visible_line, 0, std::max(0, static_cast<int>(_result_list.lines.size()) - 1));
}

std::optional<std::pair<int, int>> WxCommandPalette::selected_line_range() const
{
    const int selected_index = _session.model().selected_index;
    if (selected_index < 0 || !results_selectable())
    {
        return std::nullopt;
    }

    int first_line = -1;
    int last_line  = -1;
    for (std::size_t line_index = 0; line_index < _result_list.entry_indices.size(); ++line_index)
    {
        if (_result_list.entry_indices[line_index] != selected_index)
        {
            continue;
        }

        if (first_line < 0)
        {
            first_line = static_cast<int>(line_index);
        }

        last_line = static_cast<int>(line_index) + 1;
    }

    if (first_line < 0 || last_line <= first_line)
    {
        return std::nullopt;
    }

    return std::pair<int, int> {first_line, last_line};
}

bool WxCommandPalette::results_selectable() const
{
    return _session.model().mode != CommandPaletteMode::EnterTimestampOffset && _session.active_match_count() > 0;
}

int WxCommandPalette::visible_result_line_count() const
{
    return std::clamp(static_cast<int>(_result_list.lines.size()), 1, max_visible_result_lines);
}

int WxCommandPalette::selection_page_entry_count() const
{
    const int lines_per_entry = _session.model().mode == CommandPaletteMode::Commands ? 2 : 1;
    return std::max(1, visible_result_line_count() / lines_per_entry);
}

int WxCommandPalette::desired_height() const
{
    // Title + query rows, two separators with gaps, the result lines, and the
    // status row, framed by the panel padding.
    return 2 * panel_padding + 2 * _line_height + 2 * (section_gap + 1 + section_gap) + visible_result_line_count() * _line_height + _line_height;
}

} // namespace slayerlog
