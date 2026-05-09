#include "command_support.hpp"

#include <sstream>

#include "log_controller.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

std::string build_header_text(const std::vector<std::string>& labels)
{
    if (labels.empty())
    {
        return "No files opened (use open-file <path> or open-folder <path>)";
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        if (index > 0)
        {
            output << " | ";
        }

        output << labels[index] << ":" << (index + 1);
    }

    return output.str();
}

void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, LogController& controller, ftxui::ScreenInteractive& screen)
{
    header_text = build_header_text(tracked_sources.source_labels());
    processed_sources.set_show_source_labels(tracked_sources.source_count() > 0);
    processed_sources.rebuild_from_sources(tracked_sources);
    controller.rebuild_view(processed_sources);
    (void)processed_sources.consume_column_width_growth();
    screen.PostEvent(ftxui::Event::Custom);
}

} // namespace slayerlog
