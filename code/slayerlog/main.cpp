#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui_components/toast_component.hpp>

#include "command_line_parser.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "command_palette_view.hpp"
#include "command_registrar.hpp"
#include "command_manager.hpp"
#include "commands/command_history.hpp"
#include "debug_log.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"
#include "log_controller.hpp"
#include "timestamp/source_timestamp_parser.hpp"
#include "log_view_component.hpp"
#include "log_view2_component.hpp"
#include "notifications/ftxui_toast_notification_sink.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "settings_store.hpp"
#include "view_theme.hpp"

namespace
{

constexpr std::string_view timestamp_formats_section = "timestamp_formats";
constexpr std::string_view timestamp_format_key      = "format";

void initialize_debug_log(int argc, char** argv)
{
    slayerlog::debug_log::initialize(argc > 0 ? argv[0] : nullptr);
    SLAYERLOG_LOG_INFO("Debug log initialized at " << slayerlog::debug_log::log_file_path().string());
}

bool load_settings(slayerlog::SettingsStore& settings_store, std::string& settings_error_message)
{
    if (settings_store.load(settings_error_message))
    {
        return true;
    }

    SLAYERLOG_LOG_ERROR("Failed to load settings from " << settings_store.file_path() << ": " << settings_error_message << "; settings saves are disabled for this run");
    settings_error_message.clear();
    return false;
}

std::vector<std::string> load_timestamp_formats(slayerlog::SettingsStore& settings_store, bool settings_loaded, std::string& settings_error_message)
{
    std::vector<std::string> timestamp_formats = slayerlog::default_timestamp_formats();
    if (!settings_loaded)
    {
        SLAYERLOG_LOG_WARNING("Using built-in timestamp formats because settings failed to load from " << settings_store.file_path());
        return timestamp_formats;
    }

    if (!settings_store.ensure_default_values(timestamp_formats_section, timestamp_format_key, timestamp_formats, settings_error_message))
    {
        SLAYERLOG_LOG_WARNING("Failed to seed timestamp formats in settings file " << settings_store.file_path() << ": " << settings_error_message);
        settings_error_message.clear();
    }

    return settings_store.ini().values(timestamp_formats_section, timestamp_format_key);
}

std::shared_ptr<const slayerlog::TimestampFormatCatalog> configure_timestamp_formats(const std::vector<std::string>& timestamp_formats)
{
    auto timestamp_catalog = std::make_shared<const slayerlog::TimestampFormatCatalog>(timestamp_formats);
    slayerlog::set_default_timestamp_format_catalog(timestamp_catalog);
    return timestamp_catalog;
}

bool open_configured_sources(const slayerlog::Config& config, slayerlog::AllTrackedSources& tracked_sources)
{
    SLAYERLOG_LOG_INFO("Starting slayerlog poll_interval_ms=" << config.poll_interval_ms << " configured_sources=" << config.file_paths.size());
    for (const auto& file_path : config.file_paths)
    {
        slayerlog::LogSource source;
        try
        {
            source = slayerlog::parse_log_source(file_path);
        }
        catch (const std::exception& ex)
        {
            SLAYERLOG_LOG_ERROR("Initial source parse failed file=" << file_path << " error=" << ex.what());
            std::cerr << ex.what() << '\n';
            return false;
        }

        const auto error = open_source(tracked_sources, source);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("Initial source open failed file=" << file_path << " error=" << *error);
            std::cerr << *error << '\n';
            return false;
        }
    }

    return true;
}

std::optional<slayerlog::CommandHistory> load_command_history(slayerlog::SettingsStore& settings_store, bool settings_loaded, std::string& settings_error_message)
{
    if (!settings_loaded)
    {
        SLAYERLOG_LOG_WARNING("Command history is disabled because settings failed to load from " << settings_store.file_path());
        return std::nullopt;
    }

    std::optional<slayerlog::CommandHistory> command_history;
    command_history.emplace(settings_store);
    if (command_history->load(settings_error_message))
    {
        return command_history;
    }

    SLAYERLOG_LOG_ERROR("Failed to load command history from " << settings_store.file_path() << ": " << settings_error_message << "; command history saves are disabled for this run");
    settings_error_message.clear();
    return std::nullopt;
}

void initialize_command_palette_controller(std::optional<slayerlog::CommandPaletteController>& command_palette_controller, slayerlog::CommandPaletteModel& command_palette_model, slayerlog::CommandManager& command_manager,
                                           std::optional<slayerlog::CommandHistory>& command_history)
{
    if (command_history.has_value())
    {
        command_palette_controller.emplace(command_palette_model, command_manager, *command_history);
        return;
    }

    command_palette_controller.emplace(command_palette_model, command_manager);
}

bool handle_help_request(const slayerlog::Config& config, slayerlog::CommandManager& command_manager, slayerlog::AllProcessedSources& processed_sources, slayerlog::LogController& controller, slayerlog::AllTrackedSources& tracked_sources,
                         std::string& header_text, ftxui::ScreenInteractive& screen, const slayerlog::SettingsStore& settings_store)
{
    if (!config.show_help)
    {
        return false;
    }

    slayerlog::register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen, {}, nullptr, nullptr, settings_store.file_path()});
    std::cout << slayerlog::build_help_text(command_manager);
    return true;
}

ftxui::Component create_log_views(slayerlog::AllProcessedSources& processed_sources, slayerlog::LogController& controller, slayerlog::CommandPaletteController& command_palette_controller,
                                  std::shared_ptr<slayerlog::LogView2Component> right_view, ftxui::ScreenInteractive& screen, std::string& header_text, std::mutex& model_mutex)
{
    auto left_view = std::make_shared<slayerlog::LogViewComponent>(processed_sources, controller, command_palette_controller, screen, header_text, model_mutex);
    return ftxui::Container::Horizontal({left_view, std::move(right_view)});
}

slayerlog::CommandPaletteController* active_command_palette_controller(slayerlog::CommandPaletteController& command_palette_controller, slayerlog::CommandPaletteController& log_view2_command_palette_controller)
{
    if (command_palette_controller.is_open())
    {
        return &command_palette_controller;
    }

    if (log_view2_command_palette_controller.is_open())
    {
        return &log_view2_command_palette_controller;
    }

    return nullptr;
}

ftxui::Component create_viewer(ftxui::Component views, slayerlog::CommandPaletteController& command_palette_controller, slayerlog::CommandPaletteController& log_view2_command_palette_controller,
                               slayerlog::CommandPaletteView& command_palette_view, ftxui::ScreenInteractive& screen, std::mutex& model_mutex)
{
    auto viewer = ftxui::Renderer(views,
                                  [views, &command_palette_controller, &log_view2_command_palette_controller, &command_palette_view, &screen, &model_mutex]
                                  {
                                      auto base = views->Render();
                                      if (auto* active_palette = active_command_palette_controller(command_palette_controller, log_view2_command_palette_controller))
                                      {
                                          std::lock_guard lock(model_mutex);
                                          base = ftxui::dbox({std::move(base), command_palette_view.render(*active_palette, screen.dimy())});
                                      }
                                      return base;
                                  });

    viewer |= ftxui::CatchEvent(
        [&command_palette_controller, &log_view2_command_palette_controller, &model_mutex](ftxui::Event event)
        {
            auto* active_palette = active_command_palette_controller(command_palette_controller, log_view2_command_palette_controller);
            if (active_palette == nullptr)
            {
                return false;
            }

            std::lock_guard lock(model_mutex);
            return active_palette->handle_event(event);
        });

    return viewer;
}

std::shared_ptr<ToastHostComponent> create_toast_host(ftxui::Component viewer, ftxui::ScreenInteractive& screen)
{
    ToastHostOption toast_option;
    toast_option.screen           = &screen;
    toast_option.width            = 48;
    toast_option.max_visible      = std::numeric_limits<int>::max();
    toast_option.style.info       = slayerlog::theme::toast_info_fg;
    toast_option.style.success    = slayerlog::theme::toast_success_fg;
    toast_option.style.warning    = slayerlog::theme::toast_warning_fg;
    toast_option.style.error      = slayerlog::theme::toast_error_fg;
    toast_option.style.background = slayerlog::theme::toast_background_bg;
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

void append_sources_delta_to_processed_sources(const slayerlog::AllTrackedSources& tracked_sources, slayerlog::AllLineIndex first_new_line_index, slayerlog::AllProcessedSources& processed_sources, slayerlog::LogController& controller,
                                               ftxui::ScreenInteractive& screen)
{
    if (first_new_line_index.value < processed_sources.total_line_count())
    {
        processed_sources.replace_from_sources(tracked_sources, first_new_line_index);
        controller.rebuild_view(processed_sources);
        (void)processed_sources.consume_column_width_growth();
        screen.PostEvent(ftxui::Event::Custom);
        return;
    }

    processed_sources.append_from_sources(tracked_sources, first_new_line_index);
    if (processed_sources.consume_column_width_growth())
    {
        controller.rebuild_view(processed_sources);
    }
    else
    {
        controller.sync_view(processed_sources);
    }
    screen.PostEvent(ftxui::Event::Custom);
}

std::thread start_watcher_thread(int poll_interval_ms, slayerlog::AllTrackedSources& tracked_sources, std::mutex& model_mutex, slayerlog::AllProcessedSources& processed_sources, slayerlog::LogController& controller,
                                 ftxui::ScreenInteractive& screen, std::atomic<bool>& keep_running)
{
    return std::thread(
        [poll_interval_ms, tracked_sources = &tracked_sources, model_mutex = &model_mutex, processed_sources = &processed_sources, controller = &controller, screen = &screen, keep_running = &keep_running]
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
                    append_sources_delta_to_processed_sources(*tracked_sources, *first_new_line_index, *processed_sources, *controller, *screen);
                }
            }
        });
}

} // namespace

int main(int argc, char** argv)
{
    initialize_debug_log(argc, argv);

    const auto config       = slayerlog::parse_command_line(argc, argv);
    std::string header_text = slayerlog::build_header_text({});

    slayerlog::SettingsStore settings_store(slayerlog::default_settings_file_path());
    std::string settings_error_message;
    const bool settings_loaded = load_settings(settings_store, settings_error_message);

    const auto timestamp_formats = load_timestamp_formats(settings_store, settings_loaded, settings_error_message);
    auto timestamp_catalog       = configure_timestamp_formats(timestamp_formats);
    slayerlog::AllTrackedSources tracked_sources(timestamp_catalog);

    if (!open_configured_sources(config, tracked_sources))
    {
        return 1;
    }

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen.TrackMouse();

    std::mutex model_mutex;
    slayerlog::AllProcessedSources processed_sources;

    auto command_history = load_command_history(settings_store, settings_loaded, settings_error_message);

    slayerlog::CommandPaletteModel command_palette_model;
    slayerlog::CommandManager command_manager;
    slayerlog::CommandPaletteModel log_view2_command_palette_model;
    slayerlog::CommandManager log_view2_command_manager;
    slayerlog::CommandPaletteView command_palette_view;
    slayerlog::LogController controller;

    std::optional<slayerlog::CommandPaletteController> command_palette_controller;
    std::optional<slayerlog::CommandPaletteController> log_view2_command_palette_controller;
    initialize_command_palette_controller(command_palette_controller, command_palette_model, command_manager, command_history);
    initialize_command_palette_controller(log_view2_command_palette_controller, log_view2_command_palette_model, log_view2_command_manager, command_history);

    if (handle_help_request(config, command_manager, processed_sources, controller, tracked_sources, header_text, screen, settings_store))
    {
        return 0;
    }

    auto right_view_data = std::make_shared<slayerlog::AllProcessedSourcesLogView2Data>(processed_sources, model_mutex);
    auto right_view      = std::make_shared<slayerlog::LogView2Component>("LogView2", right_view_data, *log_view2_command_palette_controller);
    auto views           = create_log_views(processed_sources, controller, *command_palette_controller, right_view, screen, header_text, model_mutex);

    {
        std::lock_guard lock(model_mutex);
        slayerlog::reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
    }

    std::atomic<bool> keep_running = true;
    std::vector<std::thread> background_tasks;
    std::thread watcher_thread = start_watcher_thread(config.poll_interval_ms, tracked_sources, model_mutex, processed_sources, controller, screen, keep_running);

    auto viewer     = create_viewer(views, *command_palette_controller, *log_view2_command_palette_controller, command_palette_view, screen, model_mutex);
    auto toast_host = create_toast_host(viewer, screen);
    slayerlog::Notifier notifier(std::make_shared<slayerlog::FtxuiToastNotificationSink>(toast_host));
    tracked_sources.set_notifier(notifier);

    slayerlog::register_commands(command_manager, {processed_sources, controller, tracked_sources, header_text, screen, notifier, &model_mutex, &background_tasks, settings_store.file_path()});
    slayerlog::register_log_view2_commands(log_view2_command_manager, {processed_sources, controller, tracked_sources, header_text, screen, notifier, &model_mutex, &background_tasks, settings_store.file_path()}, right_view->find_manager());

    //This blocks until app is ready for shutdown.
    screen.Loop(toast_host);

    SLAYERLOG_LOG_INFO("Screen loop exited");
    keep_running = false;
    join_background_tasks(watcher_thread, background_tasks);

    SLAYERLOG_LOG_INFO("Slayerlog shutdown complete");

    return 0;
}
