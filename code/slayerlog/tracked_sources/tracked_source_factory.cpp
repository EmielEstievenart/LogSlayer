#include "tracked_source_factory.hpp"

#include <utility>

#include "tracked_source_file.hpp"
#include "tracked_source_folder.hpp"

namespace slayerlog
{

std::unique_ptr<TrackedSourceBase> create_tracked_source(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats, TrackedSourceFolder::OpenProgressCallback open_progress_callback)
{
    if (source.kind == LogSourceKind::LocalFolder)
    {
        return std::make_unique<TrackedSourceFolder>(std::move(source), std::move(source_label), std::move(timestamp_formats), std::move(open_progress_callback));
    }

    return std::make_unique<TrackedSourceFile>(std::move(source), std::move(source_label), std::move(timestamp_formats));
}

} // namespace slayerlog
