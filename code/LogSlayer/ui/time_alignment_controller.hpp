#pragma once

#include <functional>
#include <optional>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include <ftxui_components/text_view_controller.hpp>

#include "log_types.hpp"
#include "time_alignment_apply.hpp"
#include "time_alignment_model.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

class TimeAlignmentController
{
public:
    using ApplyCallback = TimeAlignmentApplyCallback;

    struct FindNavigation
    {
        std::function<bool()> next;
        std::function<bool()> previous;
        std::function<std::optional<VisibleLineIndex>()> active_visible_index;
    };

    void start(int first_visible_line, ApplyCallback apply_callback);
    void cancel();
    bool active() const;

    std::optional<int> selected_line() const;
    const std::string& status_text() const;
    bool status_is_error() const;

    bool handle_event(AllProcessedSources& processed_sources, TextViewController& text_view_controller, ftxui::Event event,
                      const std::function<std::optional<TextViewPosition>(int, int)>& text_position_at, const FindNavigation& find_navigation, const std::function<bool()>& copy_selection_to_clipboard);

private:
    void set_selected_line(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int visible_line_index, bool keep_visible);
    void move_selection(const AllProcessedSources& processed_sources, TextViewController& text_view_controller, int delta);
    bool confirm_selection(AllProcessedSources& processed_sources);

    TimeAlignmentModel _model;
};

} // namespace slayerlog
