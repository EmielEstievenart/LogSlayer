#pragma once

#include <cstddef>
#include <memory>
#include <mutex>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "LogView2/align_time_log_view2_data.hpp"
#include "notifications/notification.hpp"

class TextViewComponent;

namespace slayerlog
{

class AlignTimeSession;
class AllProcessedSources;
class AllTrackedSources;
class LogViewService;
class LogView2Data;

/// Terminal-UI controller for the dual-pane "align time" mode. It owns the transient
/// AlignTimeSession plus the two text-view panes (left = other sources, right = the
/// aligning source) and the small nudge panel, intercepts all keyboard input while
/// active, keeps both panes scrolled in lock-step on the active line, and commits the
/// previewed offset to the tracked sources (or discards it on cancel). The whole mode
/// lives on top of the normal LogView2 without disturbing the shared processed model
/// until the user commits.
class AlignTimeController
{
public:
    AlignTimeController(AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources, LogViewService& log_view, std::mutex& model_mutex, ftxui::ScreenInteractive& screen);
    // Defined in the .cpp so the unique_ptr<AlignTimeSession> member can destroy a
    // complete type (this header only forward-declares AlignTimeSession).
    ~AlignTimeController();

    void set_notifier(Notifier notifier);

    [[nodiscard]] bool active() const;

    /// Enter alignment mode for the given source. Must be called with @p model_mutex
    /// held (commands run under it). Does nothing but notify when the source cannot be
    /// aligned (no lines, or no other lines to align against).
    void begin(std::size_t aligning_source_index);

    /// Handle one terminal event while active. Returns true when the event was consumed
    /// (every keyboard/mouse event is consumed so nothing leaks to the hidden view).
    /// Must be called with @p model_mutex held.
    bool handle_event(const ftxui::Event& event);

    /// Render the split layout. Must be called WITHOUT @p model_mutex held: the panes
    /// acquire it per-frame through their data adapters.
    [[nodiscard]] ftxui::Element render(int screen_height);

private:
    std::shared_ptr<TextViewComponent> make_pane(std::shared_ptr<LogView2Data> data);
    ftxui::Element render_nudge_panel() const;
    void commit();
    void cancel();
    void deactivate();

    AllTrackedSources& _tracked_sources;
    AllProcessedSources& _processed_sources;
    LogViewService& _log_view;
    std::mutex& _model_mutex;
    ftxui::ScreenInteractive& _screen;
    Notifier _notifier;

    std::unique_ptr<AlignTimeSession> _session;
    std::shared_ptr<AlignTimeLogView2Data> _left_data;
    std::shared_ptr<AlignTimeLogView2Data> _right_data;
    std::shared_ptr<TextViewComponent> _left_pane;
    std::shared_ptr<TextViewComponent> _right_pane;
};

} // namespace slayerlog
