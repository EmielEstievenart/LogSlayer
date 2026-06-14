#include "command_support.hpp"

#include <sstream>

#include "timestamp/log_timestamp.hpp"
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

void reload_processed_sources(const AllTrackedSources& tracked_sources, std::string& header_text, AllProcessedSources& processed_sources, ftxui::ScreenInteractive& screen)
{
    header_text = build_header_text(tracked_sources.source_display_labels());
    processed_sources.rebuild_from_sources(tracked_sources);
    (void)processed_sources.consume_column_width_growth();
    screen.PostEvent(ftxui::Event::Custom);
}

std::vector<std::string> source_labels_with_offsets(const AllTrackedSources& tracked_sources)
{
    auto labels = tracked_sources.source_display_labels();
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        const auto offset = tracked_sources.source_timestamp_offset(index);
        if (offset.has_value())
        {
            labels[index] += "   [current " + format_log_timestamp_offset(*offset) + "]";
        }
    }

    return labels;
}

} // namespace slayerlog
