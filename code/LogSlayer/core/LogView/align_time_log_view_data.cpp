#include "LogView/align_time_log_view_data.hpp"

#include <algorithm>
#include <utility>

#include "align_time_session.hpp"

namespace slayerlog
{

AlignTimeLogViewData::AlignTimeLogViewData(const AlignTimeSession& session, std::mutex& mutex, Side side) : _session(session), _mutex(mutex), _side(side)
{
}

LogViewData::Lock AlignTimeLogViewData::lock() const
{
    return Lock(_mutex);
}

std::size_t AlignTimeLogViewData::size() const
{
    return _session.row_count();
}

int AlignTimeLogViewData::widest_line_width() const
{
    return _session.widest_row_width();
}

std::string AlignTimeLogViewData::to_string(std::size_t index) const
{
    if (index >= _session.row_count())
    {
        return {};
    }

    const bool owns_row = _side == Side::Left ? _session.row_kind(index) == AlignTimeSession::RowKind::Backdrop : _session.row_kind(index) == AlignTimeSession::RowKind::Aligning;
    return owns_row ? _session.render_row(index) : std::string {};
}

LogViewData::CallbackId AlignTimeLogViewData::add_update_callback(UpdateCallback callback)
{
    const CallbackId id = _next_callback_id++;
    _callbacks.emplace_back(id, std::move(callback));
    return id;
}

void AlignTimeLogViewData::remove_update_callback(CallbackId callback_id)
{
    _callbacks.erase(std::remove_if(_callbacks.begin(), _callbacks.end(), [callback_id](const auto& registration) { return registration.first == callback_id; }), _callbacks.end());
}

} // namespace slayerlog
