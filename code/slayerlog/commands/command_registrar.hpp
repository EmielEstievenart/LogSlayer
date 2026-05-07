#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "notifications/notification.hpp"

namespace slayerlog
{

class AllTrackedSources;
class CommandManager;
class CommandPaletteController;
class LogController;
class AllProcessedSources;

std::string build_header_text(const std::vector<std::string>& labels);
void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen);
void register_commands(CommandManager& command_manager, AllProcessedSources& processed_sources, LogController& controller, CommandPaletteController& command_palette_controller, std::string& header_text, ftxui::ScreenInteractive& screen,
                       AllTrackedSources& tracked_sources, Notifier notifier = {}, std::mutex* model_mutex = nullptr, std::vector<std::thread>* background_tasks = nullptr);

} // namespace slayerlog
