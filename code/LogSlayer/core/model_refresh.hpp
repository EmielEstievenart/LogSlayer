#pragma once

#include "log_types.hpp"

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class RedrawScheduler;

/// Rebuild the processed model from the tracked sources after the set of
/// sources changed, then request a redraw. The pull-based views re-render
/// straight from the processed sources, so there is no buffer to refresh.
/// Callers must hold the model mutex.
void reload_processed_sources(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler);

/// Fold newly polled lines into the processed model (replacing when earlier
/// lines changed, appending for pure streaming growth), then request a redraw.
/// Callers must hold the model mutex.
void append_sources_delta_to_processed_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_new_line_index, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler);

} // namespace slayerlog
