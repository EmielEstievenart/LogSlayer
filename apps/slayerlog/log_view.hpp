#pragma once

#include <optional>
#include <string>

#include <ftxui/component/mouse.hpp>
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
    ftxui::Element render(const AllProcessedSources& processed_sources, LogController& controller, const std::string& header_text, int screen_height, std::optional<HiddenColumnRange> hidden_column_preview = std::nullopt);

    std::optional<TextViewPosition> mouse_to_text_position(const LogController& controller, const ftxui::Mouse& mouse) const;

private:
    TextViewView _text_view;
};

} // namespace slayerlog
