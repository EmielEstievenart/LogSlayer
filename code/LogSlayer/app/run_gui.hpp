#pragma once

#include <mutex>

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class SettingsStore;
struct Config;

/// WxWidgets GUI composition root (milestone M1 of docs/wx-ui-plan.md): a
/// frame hosting the custom-drawn pull-based log view over the shared model,
/// fed by the core watcher thread. Palette, commands and toasts arrive in
/// later milestones. argc/argv are forwarded to wxEntryStart.
int run_gui(int argc, char** argv, const Config& config, SettingsStore& settings_store, bool settings_loaded, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources);

} // namespace slayerlog
