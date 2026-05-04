#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
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
#include "log_controller.hpp"
#include "timestamp/source_timestamp_parser.hpp"
#include "log_view.hpp"
#include "master_controller.hpp"
#include "master_view.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "settings_store.hpp"

namespace
{

constexpr std::string_view timestamp_formats_section = "timestamp_formats";
constexpr std::string_view timestamp_format_key      = "format";

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
    slayerlog::debug_log::initialize(argc > 0 ? argv[0] : nullptr);
    SLAYERLOG_LOG_INFO("Debug log initialized at " << slayerlog::debug_log::log_file_path().string());

    const auto config       = slayerlog::parse_command_line(argc, argv);
    std::string header_text = slayerlog::build_header_text({});

    slayerlog::SettingsStore settings_store(slayerlog::default_settings_file_path());
    std::string settings_error_message;
    if (!settings_store.load(settings_error_message))
    {
        SLAYERLOG_LOG_WARNING("Failed to load settings from " << settings_store.file_path() << ": " << settings_error_message);
        settings_error_message.clear();
    }

    if (!settings_store.ensure_default_values(timestamp_formats_section, timestamp_format_key, slayerlog::default_timestamp_formats(), settings_error_message))
    {
        SLAYERLOG_LOG_WARNING("Failed to seed timestamp formats in settings file " << settings_store.file_path() << ": " << settings_error_message);
        settings_error_message.clear();
    }

    auto timestamp_catalog = std::make_shared<const slayerlog::TimestampFormatCatalog>(settings_store.ini().values(timestamp_formats_section, timestamp_format_key));
    slayerlog::set_default_timestamp_format_catalog(timestamp_catalog);
    slayerlog::AllTrackedSources tracked_sources(timestamp_catalog);

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
            return 1;
        }

        const auto error = tracked_sources.open_source(source);
        if (error.has_value())
        {
            SLAYERLOG_LOG_ERROR("Initial source open failed file=" << file_path << " error=" << *error);
            std::cerr << *error << '\n';
            return 1;
        }
    }

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    screen.TrackMouse();

    std::mutex model_mutex;
    slayerlog::AllProcessedSources processed_sources;
    processed_sources.set_show_source_labels(tracked_sources.source_count() > 0);

    slayerlog::CommandHistory command_history(settings_store);
    if (!command_history.load(settings_error_message))
    {
        SLAYERLOG_LOG_WARNING("Failed to load settings from " << settings_store.file_path() << ": " << settings_error_message);
    }

    slayerlog::CommandPaletteModel command_palette_model;
    slayerlog::CommandManager command_manager;
    slayerlog::LogView view;
    slayerlog::CommandPaletteView command_palette_view;
    slayerlog::MasterView master_view(view, command_palette_view);
    slayerlog::LogController controller;

    slayerlog::CommandPaletteController command_palette_controller(command_palette_model, command_manager, command_history);

    if (config.show_help)
    {
        slayerlog::register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources);
        std::cout << slayerlog::build_help_text(command_manager);
        return 0;
    }

    slayerlog::MasterController master_controller(processed_sources, controller, view, screen, command_palette_controller);

    {
        std::lock_guard lock(model_mutex);
        slayerlog::reload_processed_sources(tracked_sources, header_text, processed_sources, controller, screen);
    }

    std::atomic<bool> keep_running = true;
    std::vector<std::thread> background_tasks;
    std::thread watcher_thread     = start_watcher_thread(config.poll_interval_ms, tracked_sources, model_mutex, processed_sources, controller, screen, keep_running);

    auto viewer = ftxui::Renderer(
        [&]
        {
            std::lock_guard lock(model_mutex);
            return master_view.render(processed_sources, controller, header_text, screen.dimy(), command_palette_controller);
        });

    viewer |= ftxui::CatchEvent(
        [&](ftxui::Event event)
        {
            std::lock_guard lock(model_mutex);
            return master_controller.handle_event(event);
        });

    ToastHostOption toast_option;
    toast_option.screen           = &screen;
    toast_option.width            = 48;
    toast_option.max_visible      = 3;
    toast_option.style.success    = ftxui::Color::GreenLight;
    toast_option.style.background = ftxui::Color::GrayDark;
    auto toast_host               = std::make_shared<ToastHostComponent>(viewer, toast_option);

    slayerlog::register_commands(command_manager, processed_sources, controller, command_palette_controller, header_text, screen, tracked_sources, toast_host.get(), &model_mutex, &background_tasks);

    screen.Loop(toast_host);
    SLAYERLOG_LOG_INFO("Screen loop exited");
    keep_running = false;
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

    SLAYERLOG_LOG_INFO("Slayerlog shutdown complete");

    return 0;
}
