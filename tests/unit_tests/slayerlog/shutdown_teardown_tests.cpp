#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#    include <crtdbg.h>
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui_components/toast_component.hpp>

#include "LogView/align_time_controller.hpp"
#include "LogView/log_view_data.hpp"
#include "command_manager.hpp"
#include "command_palette_controller.hpp"
#include "command_palette_model.hpp"
#include "command_palette_view.hpp"
#include "command_registrar.hpp"
#include "command_support.hpp"
#include "log_view_bridge.hpp"
#include "log_view_component.hpp"
#include "notifications/ftxui_toast_notification_sink.hpp"
#include "timestamp/timestamp_format_catalog.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/tracked_source_factory.hpp"

namespace slayerlog
{

namespace
{

// A CRT assertion (e.g. an MSVC STL iterator-debug check) normally pops a
// blocking dialog; route it to stderr and abort so the test run stays
// unattended and the failure is visible in the output.
void suppress_crt_assert_dialogs()
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif
}

class ScopedLogFile
{
public:
    explicit ScopedLogFile(std::string_view tag)
    {
        const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        _path                    = std::filesystem::temp_directory_path() / ("slayerlog_shutdown_" + std::string(tag) + "_" + unique_suffix + ".log");
    }

    ~ScopedLogFile()
    {
        std::error_code error;
        std::filesystem::remove(_path, error);
    }

    const std::filesystem::path& path() const { return _path; }

    void write(std::string_view content) const
    {
        std::ofstream output(_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to write test log file");
        }

        output << content;
    }

private:
    std::filesystem::path _path;
};

LogSource local_file_source(const std::filesystem::path& path)
{
    LogSource source;
    source.kind       = LogSourceKind::LocalFile;
    source.spec       = path.string();
    source.local_path = path.string();
    return source;
}

} // namespace

// Regression test for the "vector erase iterator outside range" debug assertion
// seen when exiting the app after opening two log files: the Notifier copy stored
// in AllTrackedSources used to keep the toast host (and with it the whole FTXUI
// component tree) alive past AllProcessedSources, so LogViewFindManager's
// destructor unregistered its callback from an already-destructed model. Build
// the same object graph as main.cpp (minus the interactive screen loop), run the
// watcher thread briefly, then let everything destruct in main's teardown order.
TEST(ShutdownTeardown, MainObjectGraphWithTwoFilesDestructsCleanly)
{
    suppress_crt_assert_dialogs();

    ScopedLogFile file_one("one");
    ScopedLogFile file_two("two");
    file_one.write("2026-04-01T10:00:00 app startup begin\n"
                   "2026-04-01T10:00:03 config loaded profile=demo\n"
                   "warm cache step 1 complete\n");
    file_two.write("2026-04-01T10:00:01 worker thread online id=7\n"
                   "2026-04-01T10:00:04 worker accepted batch size=12\n");

    {
        std::string header_text = build_header_text({});

        auto timestamp_catalog = std::make_shared<const TimestampFormatCatalog>(default_timestamp_formats());
        set_default_timestamp_format_catalog(timestamp_catalog);
        AllTrackedSources tracked_sources(timestamp_catalog);

        ASSERT_FALSE(open_source(tracked_sources, local_file_source(file_one.path())).has_value());
        ASSERT_FALSE(open_source(tracked_sources, local_file_source(file_two.path())).has_value());

        auto screen = ftxui::ScreenInteractive::Fullscreen();

        std::mutex model_mutex;
        AllProcessedSources processed_sources;

        CommandPaletteModel command_palette_model;
        CommandManager command_manager;
        CommandPaletteView command_palette_view;

        std::optional<CommandPaletteController> command_palette_controller;
        command_palette_controller.emplace(command_palette_model, command_manager);

        auto view_data = std::make_shared<AllProcessedSourcesLogViewData>(processed_sources, model_mutex);
        auto view      = std::make_shared<LogViewComponent>("LogSlayer", view_data, *command_palette_controller, [&screen] { screen.Exit(); });
        LogViewBridge log_view_bridge(*view, header_text, screen);

        AlignTimeController align_controller(tracked_sources, processed_sources, log_view_bridge, model_mutex, screen);
        log_view_bridge.set_align_controller(&align_controller);

        {
            std::lock_guard lock(model_mutex);
            reload_processed_sources(tracked_sources, header_text, processed_sources, screen);
        }

        std::atomic<bool> keep_running = true;
        std::vector<std::thread> background_tasks;
        std::thread watcher_thread(
            [&tracked_sources, &model_mutex, &processed_sources, &screen, &keep_running]
            {
                while (keep_running)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (!keep_running)
                    {
                        break;
                    }

                    std::lock_guard lock(model_mutex);
                    const auto first_new_line_index = tracked_sources.poll();
                    if (first_new_line_index.has_value())
                    {
                        if (first_new_line_index->value < processed_sources.total_line_count())
                        {
                            processed_sources.replace_from_sources(tracked_sources, *first_new_line_index);
                        }
                        else
                        {
                            processed_sources.append_from_sources(tracked_sources, *first_new_line_index);
                        }

                        (void)processed_sources.consume_column_width_growth();
                        screen.PostEvent(ftxui::Event::Custom);
                    }
                }
            });

        auto viewer = ftxui::Renderer(view,
                                      [view, &command_palette_controller, &command_palette_view, &screen, &model_mutex, &align_controller]
                                      {
                                          if (align_controller.active())
                                          {
                                              return align_controller.render(screen.dimy());
                                          }

                                          auto base = view->Render();
                                          if (command_palette_controller->is_open())
                                          {
                                              std::lock_guard lock(model_mutex);
                                              base = ftxui::dbox({std::move(base), command_palette_view.render(*command_palette_controller, screen.dimy())});
                                          }
                                          return base;
                                      });
        viewer |= ftxui::CatchEvent(
            [&command_palette_controller, &model_mutex, &align_controller](ftxui::Event event)
            {
                if (align_controller.active())
                {
                    std::lock_guard lock(model_mutex);
                    return align_controller.handle_event(event);
                }

                if (!command_palette_controller->is_open())
                {
                    return false;
                }

                std::lock_guard lock(model_mutex);
                return command_palette_controller->handle_event(event);
            });

        ToastHostOption toast_option;
        toast_option.screen = &screen;
        toast_option.width  = 48;
        auto toast_host     = std::make_shared<ToastHostComponent>(viewer, toast_option);
        Notifier notifier(std::make_shared<FtxuiToastNotificationSink>(toast_host));
        tracked_sources.set_notifier(notifier);
        align_controller.set_notifier(notifier);

        const auto settings_path = std::filesystem::temp_directory_path() / "slayerlog_shutdown_teardown_settings.ini";
        register_log_view_commands(command_manager, {processed_sources, log_view_bridge, tracked_sources, notifier, &model_mutex, &background_tasks, settings_path}, view->find_manager());

        // Let the watcher thread run a few poll cycles, as in a real session.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

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

        // Scope end: destructors run in the same relative order as main().
    }

    SUCCEED();
}

} // namespace slayerlog
