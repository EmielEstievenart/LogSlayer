#include "log_view_data.hpp"

#include <utility>

#include "tracked_sources/all_processed_sources.hpp"

namespace slayerlog
{

LogViewData::Lock::Lock(std::mutex& mutex) : _lock(mutex)
{
}

AllProcessedSourcesLogViewData::AllProcessedSourcesLogViewData(const AllProcessedSources& processed_sources, std::mutex& mutex) : _processed_sources(processed_sources), _mutex(mutex)
{
}

LogViewData::Lock AllProcessedSourcesLogViewData::lock() const
{
    return Lock(_mutex);
}

std::size_t AllProcessedSourcesLogViewData::size() const
{
    return static_cast<std::size_t>(_processed_sources.line_count());
}

int AllProcessedSourcesLogViewData::widest_line_width() const
{
    return _processed_sources.max_rendered_line_width();
}

std::string AllProcessedSourcesLogViewData::to_string(std::size_t index) const
{
    return _processed_sources.rendered_line(static_cast<int>(index));
}

LogViewData::CallbackId AllProcessedSourcesLogViewData::add_update_callback(UpdateCallback callback)
{
    return _processed_sources.add_lines_changed_callback(std::move(callback));
}

void AllProcessedSourcesLogViewData::remove_update_callback(CallbackId callback_id)
{
    _processed_sources.remove_lines_changed_callback(callback_id);
}

std::vector<std::string> AllProcessedSourcesLogViewData::include_filters() const
{
    return _processed_sources.include_filters();
}

std::vector<std::string> AllProcessedSourcesLogViewData::exclude_filters() const
{
    return _processed_sources.exclude_filters();
}

std::optional<int> AllProcessedSourcesLogViewData::hidden_before_line() const
{
    return _processed_sources.hidden_before_line_number();
}

std::optional<HiddenColumnRange> AllProcessedSourcesLogViewData::hidden_columns() const
{
    return _processed_sources.hidden_columns();
}

bool AllProcessedSourcesLogViewData::updates_paused() const
{
    return _processed_sources.updates_paused();
}

} // namespace slayerlog
