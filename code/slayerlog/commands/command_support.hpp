#pragma once

#include <string>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class LogController;

std::string build_header_text(const std::vector<std::string>& labels);
void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen);

} // namespace slayerlog
