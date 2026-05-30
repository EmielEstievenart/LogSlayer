#pragma once

#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>

#include <ftxui_components/text_view_controller.hpp>
#include <ftxui_components/text_view_view.hpp>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

class LogView
{
public:
    ftxui::Element render(const AllProcessedSources& processed_sources, LogController& controller, const std::string& header_text, int screen_height, std::optional<HiddenColumnRange> hidden_column_preview = std::nullopt, bool focused = true);

    LogEventResult handle_event(AllProcessedSources& processed_sources, LogController& controller, ftxui::Event event);

    std::optional<TextViewPosition> text_position_at(const LogController& controller, int x, int y) const;

private:
    TextViewView _text_view;
};

} // namespace slayerlog
