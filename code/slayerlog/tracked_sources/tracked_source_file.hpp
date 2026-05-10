#pragma once

#include <memory>
#include <string>
#include <vector>

#include "notifications/notification.hpp"
#include "tracked_source_base.hpp"
#include "watchers/log_watcher_base.hpp"

namespace slayerlog
{

class TrackedSourceFile : public TrackedSourceBase
{
public:
    TrackedSourceFile(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog(), Notifier notifier = {});

    bool poll() override;
    void set_timestamp_format(std::string format) override;

    void add_entries_from_raw_strings(std::vector<std::string> lines);

private:
    void try_initialize_timestamp_parser(const std::vector<std::string>& lines);
    void report_content_parse_progress(int percent);
    bool _timestamp_parser_initialized = false;
    SourceTimestampParser _timestamp_parser;
    std::unique_ptr<LogWatcherBase> _watcher;
    NotificationHandle _content_parse_progress_notification;
    bool _is_blf_source = false;
};

} // namespace slayerlog
