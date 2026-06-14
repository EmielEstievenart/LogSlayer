#pragma once

#include <string>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;

std::string build_header_text(const std::vector<std::string>& labels);

/// Reload for the pull-based LogView2: rebuilds the processed model and header
/// from the tracked sources and requests a redraw. The view re-renders from the
/// model itself, so there is no buffer to refresh.
void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, ftxui::ScreenInteractive& screen);

/** Display labels annotated with each source's current timestamp offset, for use in source pickers. */
std::vector<std::string> source_labels_with_offsets(const AllTrackedSources& tracked_sources);

} // namespace slayerlog
