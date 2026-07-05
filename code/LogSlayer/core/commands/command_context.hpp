#pragma once

#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "notifications/notification.hpp"

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class LogViewService;
class SettingsStore;

/// Services handed to every command. UI-agnostic: the view is reached through
/// the core LogViewService abstraction (not the concrete terminal controller),
/// and there is no reference to the FTXUI screen. This is what lets command
/// implementations compile into the core library; the terminal UI only supplies
/// the concrete LogViewService and renders the interactive commands.
struct CommandContext
{
    AllProcessedSources& processed_sources;
    LogViewService& log_view;
    AllTrackedSources& tracked_sources;
    Notifier notifier;
    std::mutex* model_mutex                    = nullptr;
    std::vector<std::thread>* background_tasks = nullptr;
    std::filesystem::path settings_file_path;

    /// Store behind settings_file_path, for commands that persist session
    /// configs. Null when settings failed to load (saves are disabled).
    SettingsStore* settings_store = nullptr;
};

} // namespace slayerlog
