#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tracked_source_base.hpp"
#include "watchers/log_watcher_base.hpp"

namespace slayerlog
{

class TrackedSourceFile : public TrackedSourceBase
{
public:
    TrackedSourceFile(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());

    bool poll() override;
    void set_timestamp_format(std::string format) override;

    void add_entries_from_raw_strings(std::vector<std::string> lines);

private:
    void try_initialize_timestamp_parser(const std::vector<std::string>& lines);
    bool _timestamp_parser_initialized = false;
    SourceTimestampParser _timestamp_parser;
    std::unique_ptr<LogWatcherBase> _watcher;
};

} // namespace slayerlog
