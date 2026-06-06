#include "log_view2_data.hpp"

#include <utility>

#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

LogView2Data::Lock::Lock(std::mutex& mutex) : _lock(mutex)
{
}

AllProcessedSourcesLogView2Data::AllProcessedSourcesLogView2Data(const AllProcessedSources& processed_sources, std::mutex& mutex) : _processed_sources(processed_sources), _mutex(mutex)
{
}

LogView2Data::Lock AllProcessedSourcesLogView2Data::lock() const
{
    return Lock(_mutex);
}

std::size_t AllProcessedSourcesLogView2Data::size() const
{
    return static_cast<std::size_t>(_processed_sources.line_count());
}

int AllProcessedSourcesLogView2Data::widest_line_width() const
{
    return _processed_sources.max_rendered_line_width();
}

std::string AllProcessedSourcesLogView2Data::to_string(std::size_t index) const
{
    return _processed_sources.rendered_line(static_cast<int>(index));
}

LogView2Data::CallbackId AllProcessedSourcesLogView2Data::add_update_callback(UpdateCallback callback)
{
    return _processed_sources.add_lines_changed_callback(std::move(callback));
}

void AllProcessedSourcesLogView2Data::remove_update_callback(CallbackId callback_id)
{
    _processed_sources.remove_lines_changed_callback(callback_id);
}

} // namespace slayerlog
