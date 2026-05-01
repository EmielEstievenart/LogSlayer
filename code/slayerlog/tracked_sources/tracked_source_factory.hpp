#pragma once

#include <memory>
#include <string>

#include "tracked_source_base.hpp"

namespace slayerlog
{

std::unique_ptr<TrackedSourceBase> create_tracked_source(LogSource source, std::string source_label,
                                                         std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());

} // namespace slayerlog
