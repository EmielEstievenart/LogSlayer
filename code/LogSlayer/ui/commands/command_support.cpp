#include "command_support.hpp"

#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

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
