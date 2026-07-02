#pragma once

#include <string>
#include <vector>

namespace slayerlog
{

class AllTrackedSources;

/** Display labels annotated with each source's current timestamp offset, for use in source pickers. */
std::vector<std::string> source_labels_with_offsets(const AllTrackedSources& tracked_sources);

} // namespace slayerlog
