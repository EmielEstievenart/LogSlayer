#include "LogView2/align_time_controller.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>

#include <ftxui_components/text_view_component.hpp>
#include <ftxui_components/text_view_controller.hpp>

#include "align_time_session.hpp"
#include "log_view2_utils.hpp"
#include "log_view_service.hpp"
#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "view_theme.hpp"

namespace slayerlog
{

namespace
{

ftxui::Element hints(std::initializer_list<ftxui::Element> items)
{
    std::vector<ftxui::Element> row;
    bool first = true;
    for (auto& item : items)
    {
        if (!first)
        {
            row.push_back(ftxui::text("   "));
        }
        first = false;
        row.push_back(item);
    }

    return ftxui::hbox(std::move(row));
}

} // namespace

AlignTimeController::AlignTimeController(AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources, LogViewService& log_view, std::mutex& model_mutex, ftxui::ScreenInteractive& screen)
    : _tracked_sources(tracked_sources), _processed_sources(processed_sources), _log_view(log_view), _model_mutex(model_mutex), _screen(screen)
{
}

AlignTimeController::~AlignTimeController() = default;

void AlignTimeController::set_notifier(Notifier notifier)
{
    _notifier = std::move(notifier);
}

bool AlignTimeController::active() const
{
    return _session != nullptr;
}

void AlignTimeController::begin(std::size_t aligning_source_index)
{
    auto session = std::make_unique<AlignTimeSession>(_processed_sources, aligning_source_index);
    if (!session->ready())
    {
        (void)_notifier.show({"Align time", session->status_text(), NotificationLevel::Warning});
        return;
    }

    _session    = std::move(session);
    _left_data  = std::make_shared<AlignTimeLogView2Data>(*_session, _model_mutex, AlignTimeLogView2Data::Side::Left);
    _right_data = std::make_shared<AlignTimeLogView2Data>(*_session, _model_mutex, AlignTimeLogView2Data::Side::Right);
    _left_pane  = make_pane(_left_data);
    _right_pane = make_pane(_right_data);
    _screen.PostEvent(ftxui::Event::Custom);
}

bool AlignTimeController::handle_event(const ftxui::Event& event)
{
    if (_session == nullptr)
    {
        return false;
    }

    const bool nudging = _session->phase() == AlignTimeSession::Phase::Nudge;

    if (event == ftxui::Event::Escape)
    {
        cancel();
        return true;
    }
    if (event == ftxui::Event::Return)
    {
        if (nudging)
        {
            commit();
        }
        else
        {
            _session->advance();
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::ArrowUp)
    {
        nudging ? _session->nudge(-1) : _session->move_cursor(-1);
        _screen.PostEvent(ftxui::Event::Custom);
        return true;
    }
    if (event == ftxui::Event::ArrowDown)
    {
        nudging ? _session->nudge(1) : _session->move_cursor(1);
        _screen.PostEvent(ftxui::Event::Custom);
        return true;
    }
    if (event == ftxui::Event::ArrowLeft)
    {
        // While nudging, Left/Right change the granularity instead of moving a cursor:
        // Left = coarser (toward 100 ms), Right = finer (toward 1 us).
        if (nudging)
        {
            _session->change_step(1);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::ArrowRight)
    {
        if (nudging)
        {
            _session->change_step(-1);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::PageUp)
    {
        if (!nudging)
        {
            _session->move_cursor(-10);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::PageDown)
    {
        if (!nudging)
        {
            _session->move_cursor(10);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::Home)
    {
        if (!nudging)
        {
            _session->move_cursor(-1'000'000);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::End)
    {
        if (!nudging)
        {
            _session->move_cursor(1'000'000);
            _screen.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }
    if (event == ftxui::Event::Backspace)
    {
        _session->step_back();
        _screen.PostEvent(ftxui::Event::Custom);
        return true;
    }
    if (event == ftxui::Event::Character(" "))
    {
        _session->toggle_left_selection();
        _screen.PostEvent(ftxui::Event::Custom);
        return true;
    }

    // Let redraw requests through so the watcher thread can still refresh the frame;
    // swallow every other key/mouse event so nothing reaches the hidden view underneath.
    if (event == ftxui::Event::Custom)
    {
        return false;
    }

    return true;
}

ftxui::Element AlignTimeController::render(int screen_height)
{
    (void)screen_height;
    if (_session == nullptr)
    {
        return ftxui::emptyElement();
    }

    const int row_count = static_cast<int>(_session->row_count());
    const int width     = _session->widest_row_width();
    _left_pane->update_content_size(row_count, width);
    _right_pane->update_content_size(row_count, width);

    // Keep both panes locked on the active line so their rows stay aligned across panes.
    if (const auto active_row = _session->cursor_row())
    {
        _left_pane->controller().center_on_line(*active_row);
        _right_pane->controller().center_on_line(*active_row);
    }

    ftxui::Element left_panel  = ftxui::window(ftxui::text("Other sources"), _left_pane->Render(), ftxui::LIGHT) | ftxui::flex;
    ftxui::Element right_panel = ftxui::window(ftxui::text("Aligning: " + _session->aligning_source_label()) | ftxui::color(theme::label_align_fg), _right_pane->Render(), ftxui::DOUBLE) | ftxui::flex;

    // Stack the panes side-by-side, with the nudge panel spanning the full width beneath both.
    ftxui::Element panes = ftxui::hbox({std::move(left_panel), std::move(right_panel)}) | ftxui::flex;
    return ftxui::vbox({std::move(panes), render_nudge_panel()});
}

std::shared_ptr<TextViewComponent> AlignTimeController::make_pane(std::shared_ptr<LogView2Data> data)
{
    AlignTimeController* self = this;
    TextViewComponentOption option;
    option.draw_content = [self, data](ftxui::Canvas& canvas, int first_line, int line_count, int first_col, int col_count)
    {
        auto lock                       = data->lock();
        const int total                 = static_cast<int>(data->size());
        const int visible               = std::max(0, std::min(line_count, total - first_line));
        const AlignTimeSession* session = self->_session.get();

        std::optional<int> cursor_row;
        std::optional<int> right_selected;
        std::vector<int> left_selected;
        if (session != nullptr)
        {
            cursor_row     = session->cursor_row();
            right_selected = session->right_selected_row();
            left_selected  = session->left_selected_rows();
        }

        for (int row = 0; row < visible; ++row)
        {
            const int line_index   = first_line + row;
            const std::string line = data->to_string(static_cast<std::size_t>(line_index));
            if (first_col >= static_cast<int>(line.size()))
            {
                // Blank (not-owned) rows render empty, preserving height so panes overlay.
                continue;
            }

            const int count = std::min(col_count, static_cast<int>(line.size()) - first_col);
            canvas.DrawText(0, row * 4, line.substr(static_cast<std::size_t>(first_col), static_cast<std::size_t>(count)));

            const bool is_active    = cursor_row.has_value() && *cursor_row == line_index;
            const bool is_reference = (right_selected.has_value() && *right_selected == line_index) || std::find(left_selected.begin(), left_selected.end(), line_index) != left_selected.end();
            if (is_active)
            {
                color_line(canvas, row, 0, count, theme::selected_line_fg, theme::selected_line_bg);
            }
            else if (is_reference)
            {
                color_line(canvas, row, 0, count, theme::terminal::black, theme::terminal::green_light);
            }
        }
    };

    return std::make_shared<TextViewComponent>(std::move(option));
}

ftxui::Element AlignTimeController::render_nudge_panel() const
{
    const auto phase           = _session->phase();
    const auto& status         = _session->status_text();
    ftxui::Element status_line = ftxui::text(status) | ftxui::color(_session->status_is_error() ? theme::error_fg : theme::muted);

    ftxui::Element key_line;
    switch (phase)
    {
    case AlignTimeSession::Phase::SelectRight:
        key_line = hints({theme::key_hint("Up/Down", "move"), theme::key_hint("Enter", "pick line"), theme::key_hint("Esc", "cancel")});
        break;
    case AlignTimeSession::Phase::SelectLeft:
        key_line = hints({theme::key_hint("Up/Down", "move"), theme::key_hint("Space", "mark ref"), theme::key_hint("Enter", "nudge"), theme::key_hint("Backspace", "back"), theme::key_hint("Esc", "cancel")});
        break;
    case AlignTimeSession::Phase::Nudge:
        key_line =
            hints({theme::key_hint("Up", "earlier"), theme::key_hint("Down", "later"), theme::key_hint("Left/Right", "step"), theme::key_hint("Enter", "apply"), theme::key_hint("Backspace", "back"), theme::key_hint("Esc", "cancel")});
        break;
    }

    std::vector<ftxui::Element> offset_row {
        theme::badge("OFFSET ", theme::label_align_fg),
        ftxui::text(format_log_timestamp_offset(_session->preview_offset())),
    };
    if (phase == AlignTimeSession::Phase::Nudge)
    {
        // The step only matters (and only changes) while nudging, so surface it there.
        offset_row.push_back(ftxui::text("    "));
        offset_row.push_back(theme::badge("STEP ", theme::label_align_fg));
        offset_row.push_back(ftxui::text(std::string(_session->current_step_label())));
    }
    ftxui::Element offset_line = ftxui::hbox(std::move(offset_row));

    return ftxui::window(ftxui::text("Align time"), ftxui::vbox({std::move(status_line), std::move(offset_line), ftxui::separator(), std::move(key_line)})) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 6);
}

void AlignTimeController::commit()
{
    if (_session == nullptr || !_session->can_commit())
    {
        cancel();
        return;
    }

    const LogTimestampOffset offset = _session->preview_offset();
    const std::size_t source_index  = _session->aligning_source_index();
    const std::string source_label  = _session->aligning_source_label();

    const auto error = _tracked_sources.adjust_source_timestamp_offset(source_index, offset);
    deactivate();

    if (error.has_value())
    {
        (void)_notifier.show({"Align time", *error, NotificationLevel::Error});
        _log_view.rebuild_view(_processed_sources);
        return;
    }

    _log_view.reload(_tracked_sources, _processed_sources);
    (void)_notifier.show({"Align time", "Aligned " + source_label + " by " + format_log_timestamp_offset(offset), NotificationLevel::Success});
}

void AlignTimeController::cancel()
{
    deactivate();
    (void)_notifier.show({"Align time", "Alignment cancelled", NotificationLevel::Info});
}

void AlignTimeController::deactivate()
{
    _left_pane.reset();
    _right_pane.reset();
    _left_data.reset();
    _right_data.reset();
    _session.reset();
    _screen.PostEvent(ftxui::Event::Custom);
}

} // namespace slayerlog
