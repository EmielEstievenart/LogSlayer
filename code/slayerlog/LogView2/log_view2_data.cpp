#include "log_view2_data.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "timestamp/log_timestamp.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_sources/log_line.hpp"

namespace slayerlog
{

namespace
{

std::string format_entry_time(const LogEntry& entry)
{
    const auto timestamp = effective_timestamp(entry.metadata);
    return timestamp.has_value() ? format_log_timestamp_utc(*timestamp) : std::string();
}

std::string format_entry_source(const LogEntry& entry)
{
    return std::to_string(entry.metadata.source_index + 1);
}

} // namespace

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
    int width = _tracked_sources.widest_line_width() + _widest_source_width + 1; // source column plus its trailing space
    if (_widest_time_width > 0)
    {
        width += _widest_time_width + 1; // time column plus its trailing space
    }
    return width;
}

std::string AllTrackedSourcesLogView2Data::to_string(std::size_t index) const
{
    const auto& line = _tracked_sources.all_lines()[AllLineIndex {static_cast<int>(index)}];
    if (line == nullptr)
    {
        return std::string();
    }

    const std::string time   = format_entry_time(*line);
    const std::string source = format_entry_source(*line);
    _widest_time_width       = std::max(_widest_time_width, static_cast<int>(time.size()));
    _widest_source_width     = std::max(_widest_source_width, static_cast<int>(source.size()));

    // Prefix each line with the parsed time and the source number it belongs to,
    // padded to the widest seen so far so every line aligns into fixed columns.
    std::ostringstream output;
    if (_widest_time_width > 0)
    {
        output << std::left << std::setw(_widest_time_width) << time << ' ';
    }
    output << std::right << std::setw(_widest_source_width) << source << ' ';
    output << line->text;
    return output.str();
}

} // namespace slayerlog
