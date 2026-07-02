#include "run_gui.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>

#include <wx/app.h>
#include <wx/init.h>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

#include "command_line_parser.hpp"
#include "debug_log.hpp"
#include "log_view2_data.hpp"
#include "model_refresh.hpp"
#include "settings_store.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "watcher_thread.hpp"
#include "wx_main_frame.hpp"
#include "wx_redraw_scheduler.hpp"

namespace slayerlog
{

namespace
{

/// wxApp that skips wx's own command-line handling (LogSlayer already parsed
/// argv itself) and leaves window creation to run_gui.
class LogSlayerWxApp : public wxApp
{
public:
    bool OnInit() override
    {
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
        MSWEnableDarkMode();
#endif
        return true;
    }
};

} // namespace

int run_gui(int argc, char** argv, const Config& config, SettingsStore& /*settings_store*/, bool /*settings_loaded*/, AllTrackedSources& tracked_sources, std::mutex& model_mutex, AllProcessedSources& processed_sources)
{
#ifdef _WIN32
    // Single console-subsystem exe by design (docs/wx-ui-plan.md): drop the
    // console window in GUI mode; --help and CLI errors printed before this.
    ::FreeConsole();
#endif

    wxApp::SetInstance(new LogSlayerWxApp());
    if (!wxEntryStart(argc, argv))
    {
        SLAYERLOG_LOG_ERROR("wxEntryStart failed; cannot start the GUI");
        return 1;
    }

    if (!wxTheApp->CallOnInit())
    {
        SLAYERLOG_LOG_ERROR("wxApp initialization failed; cannot start the GUI");
        wxEntryCleanup();
        return 1;
    }

    auto view_data = std::make_shared<AllProcessedSourcesLogView2Data>(processed_sources, model_mutex);

    // wx top-level windows delete themselves when closed.
    auto* frame = new WxMainFrame("LogSlayer", view_data);
    WxRedrawScheduler redraw_scheduler(*frame, [frame] { frame->on_model_updated(); });

    {
        std::lock_guard lock(model_mutex);
        reload_processed_sources(tracked_sources, processed_sources, redraw_scheduler);
    }

    std::atomic<bool> keep_running = true;
    std::thread watcher_thread     = start_watcher_thread(config.poll_interval_ms, tracked_sources, model_mutex, processed_sources, redraw_scheduler, keep_running);

    // Stop the watcher before the frame (the redraw target) is destroyed.
    frame->set_on_close(
        [&keep_running, &watcher_thread]
        {
            keep_running = false;
            if (watcher_thread.joinable())
            {
                watcher_thread.join();
            }
        });

    frame->Show(true);
    wxTheApp->SetTopWindow(frame);

    // Exception boundary: wx's default OnExceptionInMainLoop rethrows out of
    // OnRun; without this the joinable watcher thread would terminate the
    // process before any shutdown ran.
    int exit_code = 1;
    try
    {
        exit_code = wxTheApp->OnRun();
    }
    catch (const std::exception& ex)
    {
        SLAYERLOG_LOG_ERROR("Unhandled exception escaped the GUI event loop: " << ex.what());
    }
    catch (...)
    {
        SLAYERLOG_LOG_ERROR("Unhandled non-std exception escaped the GUI event loop");
    }

    SLAYERLOG_LOG_INFO("GUI loop exited");
    keep_running = false;
    if (watcher_thread.joinable())
    {
        watcher_thread.join();
    }

    wxTheApp->OnExit();
    wxEntryCleanup();

    SLAYERLOG_LOG_INFO("Slayerlog shutdown complete");

    return exit_code;
}

} // namespace slayerlog
