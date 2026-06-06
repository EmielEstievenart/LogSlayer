#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "log_batch.hpp"
#include "log_source.hpp"
#include "notifications/notification.hpp"
#include "timestamp/source_timestamp_parser.hpp"
#include "log_types.hpp"

namespace slayerlog
{

class TrackedSourceBase;

class AllTrackedSources
{
public:
    using LinesChangedCallback = std::function<void(VisibleLineIndex)>;
    using CallbackId           = std::size_t;

    explicit AllTrackedSources(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());
    ~AllTrackedSources();

    std::optional<std::string> open_source(const LogSource& source, Notifier notifier = {});
    std::optional<std::string> add_opened_source(std::unique_ptr<TrackedSourceBase> source_state);
    std::optional<std::string> close_source(std::size_t source_index, std::string* closed_label = nullptr);

    std::optional<AllLineIndex> poll();

    const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& all_lines() const;
    int line_count() const;
    int widest_line_width() const;

    std::size_t source_count() const;
    bool empty() const;
    bool is_source_open(const LogSource& candidate_source) const;
    std::vector<std::string> source_labels() const;
    const std::vector<std::string>& timestamp_formats() const;
    std::shared_ptr<const TimestampFormatCatalog> timestamp_format_catalog() const;
    void set_notifier(Notifier notifier);
    CallbackId add_lines_changed_callback(LinesChangedCallback callback) const;
    void remove_lines_changed_callback(CallbackId callback_id) const;
    std::optional<std::string> set_source_timestamp_format(std::size_t source_index, const std::string& format);
    std::optional<std::string> set_source_timestamp_offset(std::size_t source_index, LogTimestampOffset offset);
    std::optional<std::string> clear_source_timestamp_offset(std::size_t source_index);

private:
    struct CallbackRegistration
    {
        CallbackId id;
        LinesChangedCallback callback;
    };

    void rebuild_source_labels();
    void rebuild_all_lines();
    void notify_lines_changed(VisibleLineIndex first_changed_line) const;
    void update_widest_line_width(const std::shared_ptr<LogEntry>& line);
    void append_source_range(std::vector<LogBatchSourceRange>& source_ranges, const TrackedSourceBase& source, std::size_t source_index, std::size_t first_entry_index) const;
    void append_merged_lines(const std::vector<std::shared_ptr<LogEntry>>& lines);

    std::vector<std::unique_ptr<TrackedSourceBase>> _sources;
    IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex> _all_lines;
    int _widest_line_width = 0;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
    Notifier _notifier;
    mutable std::mutex _callbacks_mutex;
    mutable std::vector<CallbackRegistration> _callbacks;
    mutable CallbackId _next_callback_id = 1;
};

} // namespace slayerlog
