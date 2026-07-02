#include "run_gui.hpp"

#include <iostream>

namespace slayerlog
{

// Compiled instead of run_gui.cpp when LOGSLAYER_ENABLE_WX_UI is off (Linux
// default: building wxWidgets needs GTK3/X11 dev packages).
int run_gui(int /*argc*/, char** /*argv*/, const Config& /*config*/, SettingsStore& /*settings_store*/, bool /*settings_loaded*/, AllTrackedSources& /*tracked_sources*/, std::mutex& /*model_mutex*/,
            AllProcessedSources& /*processed_sources*/)
{
    std::cerr << "This build has no WxWidgets GUI; reconfigure with -DLOGSLAYER_ENABLE_WX_UI=ON or run with --ui tui.\n";
    return 1;
}

} // namespace slayerlog
