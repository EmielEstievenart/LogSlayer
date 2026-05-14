#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "notifications/notification.hpp"

namespace slayerlog
{

class AllProcessedSources;
class AllTrackedSources;
class LogController;

struct CommandContext
{
    AllProcessedSources& processed_sources;
    LogController& log_controller;
    AllTrackedSources& tracked_sources;
    std::string& header_text;
    ftxui::ScreenInteractive& screen;
    Notifier notifier;
    std::mutex* model_mutex                    = nullptr;
    std::vector<std::thread>* background_tasks = nullptr;
    std::filesystem::path settings_file_path;
};

} // namespace slayerlog
