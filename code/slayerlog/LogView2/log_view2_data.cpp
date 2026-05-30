#include "log_view2_data.hpp"

#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

LogView2Data::Lock::Lock(std::mutex& mutex) : _lock(mutex)
{
}

AllTrackedSourcesLogView2Data::AllTrackedSourcesLogView2Data(const AllTrackedSources& tracked_sources, std::mutex& mutex) : _tracked_sources(tracked_sources), _mutex(mutex)
{
}

LogView2Data::Lock AllTrackedSourcesLogView2Data::lock() const
{
    return Lock(_mutex);
}

std::size_t AllTrackedSourcesLogView2Data::size() const
{
    return _tracked_sources.all_lines().size();
}

int AllTrackedSourcesLogView2Data::widest_line_width() const
{
    return _tracked_sources.widest_line_width();
}

std::string AllTrackedSourcesLogView2Data::to_string(std::size_t index) const
{
    const auto& line = _tracked_sources.all_lines()[AllLineIndex {static_cast<int>(index)}];
    return line != nullptr ? line->text : std::string();
}

} // namespace slayerlog
