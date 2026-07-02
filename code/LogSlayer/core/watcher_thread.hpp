#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class RedrawScheduler;

/// Spawn the background polling thread shared by every UI composition: each
/// poll_interval_ms it polls the tracked sources under the model mutex, folds
/// any new lines into the processed model, and requests a redraw. Stops after
/// keep_running turns false (shutdown latency is bounded by one poll
/// interval); the caller joins the returned thread.
std::thread start_watcher_thread(int poll_interval_ms, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources, RedrawScheduler& redraw_scheduler, std::atomic<bool>& keep_running);

} // namespace slayerlog
