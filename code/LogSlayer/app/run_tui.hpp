#pragma once

#include <mutex>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class SettingsStore;
struct Config;

/// FTXUI terminal-UI composition root: builds the LogView2 stack, command
/// palette, toasts and the core watcher thread over the shared model, then
/// blocks in the screen loop until the user quits.
int run_tui(const Config& config, SettingsStore& settings_store, bool settings_loaded, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources);

} // namespace slayerlog
