#include <ftxui_components/text_view_controller.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#    define NOMINMAX
#    include <windows.h>
#endif

namespace
{

bool is_before(const TextViewPosition& lhs, const TextViewPosition& rhs)
{
    return lhs.line_index < rhs.line_index || (lhs.line_index == rhs.line_index && lhs.column < rhs.column);
}

#ifndef _WIN32

bool env_var_is_set(const char* name)
{
    const auto* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

bool is_ssh_session()
{
    return env_var_is_set("SSH_CONNECTION") || env_var_is_set("SSH_CLIENT") || env_var_is_set("SSH_TTY");
}

bool write_text_to_command(const char* command, const std::string& text)
{
    auto* pipe = popen(command, "w");
    if (pipe == nullptr)
    {
        return false;
    }

    const std::size_t bytes_written = std::fwrite(text.data(), 1, text.size(), pipe);
    const int status                = pclose(pipe);
    return bytes_written == text.size() && status == 0;
}

std::string base64_encode(const std::string& text)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((text.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= text.size())
    {
        const unsigned int value = (static_cast<unsigned char>(text[index]) << 16) | (static_cast<unsigned char>(text[index + 1]) << 8) | static_cast<unsigned char>(text[index + 2]);
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back(alphabet[(value >> 6) & 0x3F]);
        encoded.push_back(alphabet[value & 0x3F]);
        index += 3;
    }

    const std::size_t remainder = text.size() - index;
    if (remainder == 1)
    {
        const unsigned int value = static_cast<unsigned char>(text[index]) << 16;
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back('=');
        encoded.push_back('=');
    }
    else if (remainder == 2)
    {
        const unsigned int value = (static_cast<unsigned char>(text[index]) << 16) | (static_cast<unsigned char>(text[index + 1]) << 8);
        encoded.push_back(alphabet[(value >> 18) & 0x3F]);
        encoded.push_back(alphabet[(value >> 12) & 0x3F]);
        encoded.push_back(alphabet[(value >> 6) & 0x3F]);
        encoded.push_back('=');
    }

    return encoded;
}

std::string build_osc52_sequence(const std::string& text)
{
    const std::string payload = "52;c;" + base64_encode(text) + '\a';

    if (env_var_is_set("TMUX"))
    {
        return "\x1bPtmux;\x1b]" + payload + "\x1b\\";
    }

    const auto* term = std::getenv("TERM");
    if (term != nullptr && std::string(term).rfind("screen", 0) == 0)
    {
        return "\x1bP\x1b]" + payload + "\x1b\\";
    }

    return "\x1b]" + payload;
}

bool write_text_to_terminal_clipboard(const std::string& text)
{
    auto* terminal = std::fopen("/dev/tty", "w");
    if (terminal == nullptr)
    {
        return false;
    }

    const auto sequence      = build_osc52_sequence(text);
    const auto bytes_written = std::fwrite(sequence.data(), 1, sequence.size(), terminal);
    const bool flushed       = std::fflush(terminal) == 0;
    std::fclose(terminal);

    return bytes_written == sequence.size() && flushed;
}

bool copy_with_local_clipboard_tools(const std::string& text)
{
#    ifdef __APPLE__
    if (write_text_to_command("pbcopy 2>/dev/null", text))
    {
        return true;
    }
#    endif

    if (write_text_to_command("wl-copy --type text/plain;charset=utf-8 2>/dev/null", text))
    {
        return true;
    }

    if (write_text_to_command("xclip -in -selection clipboard 2>/dev/null", text))
    {
        return true;
    }

    if (write_text_to_command("xsel --clipboard --input 2>/dev/null", text))
    {
        return true;
    }

    return false;
}

bool copy_text_to_clipboard_on_unix(const std::string& text)
{
    if (is_ssh_session())
    {
        return write_text_to_terminal_clipboard(text) || copy_with_local_clipboard_tools(text);
    }

    return copy_with_local_clipboard_tools(text) || write_text_to_terminal_clipboard(text);
}

#endif

bool copy_text_to_clipboard(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }

#ifdef _WIN32
    const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wide_length <= 0)
    {
        return false;
    }

    auto* memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wide_length) * sizeof(wchar_t));
    if (memory == nullptr)
    {
        return false;
    }

    auto* wide_text = static_cast<wchar_t*>(GlobalLock(memory));
    if (wide_text == nullptr)
    {
        GlobalFree(memory);
        return false;
    }

    const int converted = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide_text, wide_length);
    GlobalUnlock(memory);
    if (converted <= 0)
    {
        GlobalFree(memory);
        return false;
    }

    if (!OpenClipboard(nullptr))
    {
        GlobalFree(memory);
        return false;
    }

    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr)
    {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    CloseClipboard();
    return true;
#else
    return copy_text_to_clipboard_on_unix(text);
#endif
}

} // namespace

// --- Content management ---

void TextViewController::set_content(int total_line_count, int max_line_width, TextViewController::LineAccessor line_at)
{
    _total_line_count = std::max(0, total_line_count);
    _max_line_width   = std::max(0, max_line_width);
    _line_at          = std::move(line_at);
    clear_selection();
    if (_follow_bottom)
    {
        _first_visible_line = max_first_visible_line();
    }
    else
    {
        clamp_scroll_position();
    }
    _first_visible_col = std::min(_first_visible_col, max_first_visible_col());
}

void TextViewController::update_content_size(int total_line_count, int max_line_width)
{
    _total_line_count = std::max(0, total_line_count);
    _max_line_width   = std::max(0, max_line_width);

    if (_follow_bottom)
    {
        _first_visible_line = max_first_visible_line();
    }
    else
    {
        clamp_scroll_position();
    }

    _first_visible_col = std::min(_first_visible_col, max_first_visible_col());
}

// --- Viewport ---

void TextViewController::update_viewport_line_count(int viewport_line_count)
{
    _viewport_line_count = std::max(1, viewport_line_count);
    if (_follow_bottom)
    {
        _first_visible_line = max_first_visible_line();
        return;
    }
    clamp_scroll_position();
}

void TextViewController::update_viewport_col_count(int viewport_col_count)
{
    _viewport_col_count = std::max(1, viewport_col_count);
    _first_visible_col  = std::min(_first_visible_col, max_first_visible_col());
}

// --- Scrolling ---

void TextViewController::scroll_up(int amount)
{
    const int step      = std::max(1, amount);
    _first_visible_line = std::max(0, _first_visible_line - step);
    _follow_bottom      = _first_visible_line >= max_first_visible_line();
}

void TextViewController::scroll_down(int amount)
{
    const int step      = std::max(1, amount);
    _first_visible_line = std::min(max_first_visible_line(), _first_visible_line + step);
    _follow_bottom      = _first_visible_line >= max_first_visible_line();
}

void TextViewController::page_up()
{
    scroll_up(std::max(1, normalize_viewport_line_count() - 1));
}

void TextViewController::page_down()
{
    scroll_down(std::max(1, normalize_viewport_line_count() - 1));
}

void TextViewController::scroll_to_top()
{
    _first_visible_line = 0;
    _follow_bottom      = false;
}

void TextViewController::scroll_to_bottom()
{
    _first_visible_line = max_first_visible_line();
    _follow_bottom      = true;
}

void TextViewController::scroll_left(int amount)
{
    const int step     = std::max(1, amount);
    _first_visible_col = std::max(0, _first_visible_col - step);
}

void TextViewController::scroll_right(int amount)
{
    const int step     = std::max(1, amount);
    _first_visible_col = std::min(max_first_visible_col(), _first_visible_col + step);
}

void TextViewController::center_on_line(int line_index)
{
    _first_visible_line = std::clamp(line_index - normalize_viewport_line_count() / 2, 0, max_first_visible_line());
    _follow_bottom      = _first_visible_line >= max_first_visible_line();
}

// --- Column highlight ---

void TextViewController::set_background_column_range(int col_start, int col_end, ftxui::Color color)
{
    _col_highlight.col_start = col_start;
    _col_highlight.col_end   = col_end;
    _col_highlight.color     = color;
    _col_highlight.active    = true;
}

void TextViewController::clear_background_column_range()
{
    _col_highlight.active = false;
}

// --- Text selection ---

void TextViewController::begin_selection(TextViewPosition position)
{
    _selection_anchor      = clamp_selection_position(position);
    _selection_focus       = _selection_anchor;
    _selection_in_progress = _selection_anchor.has_value();
}

void TextViewController::update_selection(TextViewPosition position)
{
    if (!_selection_in_progress || !_selection_anchor.has_value())
    {
        return;
    }
    _selection_focus = clamp_selection_position(position);
}

void TextViewController::end_selection(std::optional<TextViewPosition> position)
{
    _selection_in_progress = false;
    if (position.has_value() && _selection_anchor.has_value())
    {
        _selection_focus = clamp_selection_position(*position);
    }
}

void TextViewController::clear_selection()
{
    _selection_anchor.reset();
    _selection_focus.reset();
    _selection_in_progress = false;
}

bool TextViewController::selection_in_progress() const
{
    return _selection_in_progress;
}

std::optional<std::pair<TextViewPosition, TextViewPosition>> TextViewController::selection_bounds() const
{
    if (!_selection_anchor.has_value() || !_selection_focus.has_value() || _total_line_count == 0)
    {
        return std::nullopt;
    }

    auto start = clamp_selection_position(*_selection_anchor);
    auto end   = clamp_selection_position(*_selection_focus);
    if (is_before(end, start))
    {
        std::swap(start, end);
    }
    return std::pair(start, end);
}

std::string TextViewController::selection_text() const
{
    const auto bounds = selection_bounds();
    if (!bounds.has_value())
    {
        return {};
    }

    const auto [start, end] = *bounds;
    std::ostringstream output;
    for (int line_index = start.line_index; line_index <= end.line_index; ++line_index)
    {
        const auto& line           = line_at(line_index);
        const int line_start       = (line_index == start.line_index) ? start.column : 0;
        const int line_end         = (line_index == end.line_index) ? end.column : static_cast<int>(line.size());
        const int clamped_start    = std::clamp(line_start, 0, static_cast<int>(line.size()));
        const int clamped_end      = std::clamp(line_end, clamped_start, static_cast<int>(line.size()));
        const auto selection_count = static_cast<std::size_t>(clamped_end - clamped_start);

        output << line.substr(static_cast<std::size_t>(clamped_start), selection_count);
        if (line_index != end.line_index)
        {
            output << '\n';
        }
    }

    return output.str();
}

// --- Clipboard ---

bool TextViewController::copy_selection_to_clipboard() const
{
    return copy_text_to_clipboard(selection_text());
}

// --- Event handling ---

TextViewEventResult TextViewController::parse_event(ftxui::Event event, const std::function<std::optional<TextViewPosition>(const ftxui::Mouse&)>& mouse_to_text_position)
{
    const int fast_horizontal_step = std::max(1, (_viewport_col_count - 1) / 2);

    if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape)
    {
        return {true, true};
    }

    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k'))
    {
        scroll_up(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j'))
    {
        scroll_down(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowLeft)
    {
        scroll_left(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowLeftCtrl)
    {
        scroll_left(fast_horizontal_step);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowRight)
    {
        scroll_right(1);
        return {true, false};
    }

    if (event == ftxui::Event::ArrowRightCtrl)
    {
        scroll_right(fast_horizontal_step);
        return {true, false};
    }

    if (event == ftxui::Event::PageUp)
    {
        page_up();
        return {true, false};
    }

    if (event == ftxui::Event::PageDown)
    {
        page_down();
        return {true, false};
    }

    if (event == ftxui::Event::Home)
    {
        scroll_to_top();
        return {true, false};
    }

    if (event == ftxui::Event::End)
    {
        scroll_to_bottom();
        return {true, false};
    }

    // Ctrl+C: copy selection
    if (event == ftxui::Event::C)
    {
        return {copy_selection_to_clipboard(), false};
    }

    if (event.is_mouse())
    {
        // Mouse selection
        if (mouse_to_text_position && event.mouse().button == ftxui::Mouse::Left)
        {
            const auto position = mouse_to_text_position(event.mouse());
            if (event.mouse().motion == ftxui::Mouse::Pressed)
            {
                if (position.has_value())
                {
                    begin_selection(*position);
                    return {true, false};
                }
                clear_selection();
                return {false, false};
            }

            if (event.mouse().motion == ftxui::Mouse::Moved && _selection_in_progress)
            {
                if (position.has_value())
                {
                    update_selection(*position);
                    return {true, false};
                }
            }

            if (event.mouse().motion == ftxui::Mouse::Released)
            {
                end_selection(position);
                return {position.has_value(), false};
            }
        }

        // Right-click: copy selection
        if (mouse_to_text_position && event.mouse().button == ftxui::Mouse::Right && event.mouse().motion == ftxui::Mouse::Pressed)
        {
            return {copy_selection_to_clipboard(), false};
        }

        // Mouse wheel
        if (event.mouse().button == ftxui::Mouse::WheelUp)
        {
            scroll_up(1);
            return {true, false};
        }

        if (event.mouse().button == ftxui::Mouse::WheelDown)
        {
            scroll_down(1);
            return {true, false};
        }
    }

    return {false, false};
}

// --- Render ---

TextViewRenderData TextViewController::render_data() const
{
    const int safe_viewport = normalize_viewport_line_count();
    const int total         = _total_line_count;
    const int max_first     = std::max(0, total - safe_viewport);
    const int first         = std::clamp(_first_visible_line, 0, max_first);

    const int safe_col_viewport = std::max(1, _viewport_col_count);
    const int first_col         = std::max(0, _first_visible_col);

    TextViewRenderData data;
    data.total_lines         = total;
    data.first_visible_line  = first;
    data.viewport_line_count = safe_viewport;
    data.first_visible_col   = first_col;
    data.max_line_width      = _max_line_width;
    data.viewport_col_count  = safe_col_viewport;
    data.col_highlight       = _col_highlight;

    const int end = std::min(total, first + safe_viewport);

    // Selection decorations
    const auto selected_range = selection_bounds();
    if (selected_range.has_value())
    {
        for (int line_index = selected_range->first.line_index; line_index <= selected_range->second.line_index; ++line_index)
        {
            if (line_index < first || line_index >= end)
            {
                continue;
            }

            const auto& line          = line_at(line_index);
            const int selection_start = (line_index == selected_range->first.line_index) ? selected_range->first.column : 0;
            const int selection_end   = (line_index == selected_range->second.line_index) ? selected_range->second.column : static_cast<int>(line.size());
            const int clamped_start   = std::clamp(selection_start, 0, static_cast<int>(line.size()));
            const int clamped_end     = std::clamp(selection_end, clamped_start, static_cast<int>(line.size()));
            if (clamped_start == clamped_end)
            {
                continue;
            }

            TextViewStyle style;
            style.inverted = true;

            TextViewRangeDecoration decoration;
            decoration.line_index = line_index;
            decoration.col_start  = clamped_start;
            decoration.col_end    = clamped_end;
            decoration.style      = style;
            data.range_decorations.push_back(decoration);
        }
    }

    return data;
}

// --- Accessors ---

int TextViewController::first_visible_line() const
{
    return std::clamp(_first_visible_line, 0, max_first_visible_line());
}

int TextViewController::first_visible_col() const
{
    return std::clamp(_first_visible_col, 0, max_first_visible_col());
}

bool TextViewController::follow_bottom() const
{
    return _follow_bottom;
}

int TextViewController::viewport_line_count() const
{
    return _viewport_line_count;
}

int TextViewController::viewport_col_count() const
{
    return _viewport_col_count;
}

// --- Private ---

int TextViewController::normalize_viewport_line_count() const
{
    return std::max(1, _viewport_line_count);
}

int TextViewController::max_first_visible_line() const
{
    return std::max(0, _total_line_count - normalize_viewport_line_count());
}

int TextViewController::max_first_visible_col() const
{
    return std::max(0, _max_line_width - _viewport_col_count);
}

void TextViewController::clamp_scroll_position()
{
    _first_visible_line = std::clamp(_first_visible_line, 0, max_first_visible_line());
}

TextViewPosition TextViewController::clamp_selection_position(TextViewPosition position) const
{
    if (_total_line_count == 0)
    {
        return TextViewPosition {0, 0};
    }

    position.line_index    = std::clamp(position.line_index, 0, _total_line_count - 1);
    const auto line_length = static_cast<int>(line_at(position.line_index).size());
    position.column        = std::clamp(position.column, 0, line_length);
    return position;
}

const std::string& TextViewController::line_at(int index) const
{
    if (!_line_at || index < 0 || index >= _total_line_count)
    {
        throw std::out_of_range("TextViewController::line_at index out of range");
    }

    return _line_at(index);
}
