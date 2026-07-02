#include "model_refresh.hpp"

#include "redraw_scheduler.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

void reload_processed_sources(const AllTrackedSources& tracked_sources, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler)
{
    processed_sources.rebuild_from_sources(tracked_sources);
    (void)processed_sources.consume_column_width_growth();
    redraw_scheduler.request_redraw();
}

void append_sources_delta_to_processed_sources(const AllTrackedSources& tracked_sources, AllLineIndex first_new_line_index, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler)
{
    if (first_new_line_index.value < processed_sources.total_line_count())
    {
        processed_sources.replace_from_sources(tracked_sources, first_new_line_index);
    }
    else
    {
        processed_sources.append_from_sources(tracked_sources, first_new_line_index);
    }

    (void)processed_sources.consume_column_width_growth();
    redraw_scheduler.request_redraw();
}

} // namespace slayerlog
