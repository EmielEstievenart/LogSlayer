#include "run_tui.hpp"

#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui_components/toast_component.hpp>

#include "LogView2/align_time_controller.hpp"
#include "command_history_loader.hpp"
#include "command_line_parser.hpp"
#include "command_manager.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "command_palette_view.hpp"
#include "command_registrar.hpp"
#include "commands/command_history.hpp"
#include "debug_log.hpp"
#include "ftxui_redraw_scheduler.hpp"
#include "log_view2_bridge.hpp"
#include "log_view2_component.hpp"
#include "model_refresh.hpp"
#include "notifications/ftxui_toast_notification_sink.hpp"
#include "settings_store.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "view_theme.hpp"
#include "watcher_thread.hpp"

namespace slayerlog
{

namespace
{

void initialize_command_palette_controller(std::optional<CommandPaletteController>& command_palette_controller, CommandPaletteModel& command_palette_model, CommandManager& command_manager, std::optional<CommandHistory>& command_history)
{
    if (command_history.has_value())
    {
        command_palette_controller.emplace(command_palette_model, command_manager, *command_history);
        return;
    }

    command_palette_controller.emplace(command_palette_model, command_manager);
}

ftxui::Component create_viewer(ftxui::Component view, CommandPaletteController& command_palette_controller, CommandPaletteView& command_palette_view, ftxui::ScreenInteractive& screen, std::mutex& model_mutex,
                               AlignTimeController& align_controller)
{
    auto viewer = ftxui::Renderer(view,
                                  [view, &command_palette_controller, &command_palette_view, &screen, &model_mutex, &align_controller]
                                  {
                                      // The alignment mode takes over the whole view; it acquires the model mutex
                                      // itself (per pane), so do not hold it here.
                                      if (align_controller.active())
                                      {
                                          return align_controller.render(screen.dimy());
                                      }

                                      auto base = view->Render();
                                      if (command_palette_controller.is_open())
                                      {
                                          std::lock_guard lock(model_mutex);
                                          base = ftxui::dbox({std::move(base), command_palette_view.render(command_palette_controller, screen.dimy())});
                                      }
                                      return base;
                                  });

    viewer |= ftxui::CatchEvent(
        [&command_palette_controller, &model_mutex, &align_controller](ftxui::Event event)
        {
            // While aligning, the controller owns all input (it swallows everything but
            // redraw requests so nothing leaks to the hidden view).
            if (align_controller.active())
            {
                std::lock_guard lock(model_mutex);
                return align_controller.handle_event(event);
            }

            if (!command_palette_controller.is_open())
            {
                return false;
            }

            std::lock_guard lock(model_mutex);
            return command_palette_controller.handle_event(event);
        });

    return viewer;
}

std::shared_ptr<ToastHostComponent> create_toast_host(ftxui::Component viewer, ftxui::ScreenInteractive& screen)
{
    ToastHostOption toast_option;
    toast_option.screen           = &screen;
    toast_option.width            = 48;
    toast_option.max_visible      = std::numeric_limits<int>::max();
    toast_option.style.info       = theme::toast_info_fg;
    toast_option.style.success    = theme::toast_success_fg;
    toast_option.style.warning    = theme::toast_warning_fg;
    toast_option.style.error      = theme::toast_error_fg;
    toast_option.style.background = theme::toast_background_bg;
    return std::make_shared<ToastHostComponent>(viewer, toast_option);
}

void join_background_tasks(std::thread& watcher_thread, std::vector<std::thread>& background_tasks)
{
    if (watcher_thread.joinable())
    {
        watcher_thread.join();
    }

    for (auto& background_task : background_tasks)
    {
        if (background_task.joinable())
        {
            background_task.join();
        }
    }
}

} // namespace

int run_tui(const Config& config, SettingsStore& settings_store, bool settings_loaded, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources)
{
    std::string settings_error_message;

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen.TrackMouse();
    FtxuiRedrawScheduler redraw_scheduler(screen);

    auto command_history = load_command_history(settings_store, settings_loaded, settings_error_message);

    CommandPaletteModel command_palette_model;
    CommandManager command_manager;
    CommandPaletteView command_palette_view;

    std::optional<CommandPaletteController> command_palette_controller;
    initialize_command_palette_controller(command_palette_controller, command_palette_model, command_manager, command_history);

    auto view_data = std::make_shared<AllProcessedSourcesLogView2Data>(processed_sources, model_mutex);
    auto view      = std::make_shared<LogView2Component>("LogSlayer", view_data, *command_palette_controller, [&screen] { screen.Exit(); });
    LogView2Bridge log_view_bridge(*view, redraw_scheduler);

    AlignTimeController align_controller(tracked_sources, processed_sources, log_view_bridge, model_mutex, redraw_scheduler);
    log_view_bridge.set_align_controller(&align_controller);

    {
        std::lock_guard lock(model_mutex);
        reload_processed_sources(tracked_sources, processed_sources, redraw_scheduler);
    }

    std::atomic<bool> keep_running = true;
    std::vector<std::thread> background_tasks;
    std::thread watcher_thread = start_watcher_thread(config.poll_interval_ms, tracked_sources, model_mutex, processed_sources, redraw_scheduler, keep_running);

    auto viewer     = create_viewer(view, *command_palette_controller, command_palette_view, screen, model_mutex, align_controller);
    auto toast_host = create_toast_host(viewer, screen);
    Notifier notifier(std::make_shared<FtxuiToastNotificationSink>(toast_host));
    tracked_sources.set_notifier(notifier);
    align_controller.set_notifier(notifier);

    register_log_view2_commands(command_manager, {processed_sources, log_view_bridge, tracked_sources, notifier, &model_mutex, &background_tasks, settings_store.file_path()}, view->find_manager(), command_palette_controller->session());

    //This blocks until app is ready for shutdown.
    // Exception boundary: without it an escaping exception would destroy the
    // joinable watcher thread and terminate the process before any shutdown.
    int exit_code = 0;
    try
    {
        screen.Loop(toast_host);
    }
    catch (const std::exception& ex)
    {
        SLAYERLOG_LOG_ERROR("Unhandled exception escaped the TUI event loop: " << ex.what());
        exit_code = 1;
    }
    catch (...)
    {
        SLAYERLOG_LOG_ERROR("Unhandled non-std exception escaped the TUI event loop");
        exit_code = 1;
    }

    SLAYERLOG_LOG_INFO("Screen loop exited");
    keep_running = false;
    join_background_tasks(watcher_thread, background_tasks);

    SLAYERLOG_LOG_INFO("Slayerlog shutdown complete");

    return exit_code;
}

} // namespace slayerlog
