#include "watcher_thread.hpp"

#include <chrono>

#include "model_refresh.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

std::thread start_watcher_thread(int poll_interval_ms, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler, std::atomic<bool>& keep_running)
{
    return std::thread(
        [poll_interval_ms, tracked_sources = &tracked_sources, model_mutex = &model_mutex, processed_sources = &processed_sources, redraw_scheduler = &redraw_scheduler, keep_running = &keep_running]
        {
            while (*keep_running)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
                if (!*keep_running)
                {
                    break;
                }

                std::lock_guard lock(*model_mutex);
                const auto first_new_line_index = tracked_sources->poll();
                if (first_new_line_index.has_value())
                {
                    append_sources_delta_to_processed_sources(*tracked_sources, *first_new_line_index, *processed_sources, *redraw_scheduler);
                }
            }
        });
}

} // namespace slayerlog
