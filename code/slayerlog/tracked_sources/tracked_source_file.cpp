#include "tracked_source_file.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

#include "watchers/blf_file_watcher.hpp"
#include "watchers/log_watcher_factory.hpp"
#include "watchers/zstd_file_watcher.hpp"

namespace slayerlog
{

namespace
{

bool has_zstd_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".zst";
}

bool has_blf_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".blf";
}

std::string source_basename_for_progress(const LogSource& source)
{
    return std::filesystem::path(source.local_path).filename().string();
}

int rounded_progress_percent(std::size_t completed_count, std::size_t total_count)
{
    if (total_count == 0)
    {
        return 100;
    }

    return static_cast<int>(((completed_count * 100) + (total_count / 2)) / total_count);
}

} // namespace

TrackedSourceFile::TrackedSourceFile(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats, Notifier notifier)
    : TrackedSourceBase(std::move(source), std::move(source_label), std::move(timestamp_formats)), _content_parse_progress_notification(notifier)
{
    const LogSource& file_source = this->source();
    _is_blf_source = file_source.kind == LogSourceKind::LocalFile && has_blf_extension(file_source.local_path);
    if (file_source.kind == LogSourceKind::LocalFile && has_zstd_extension(file_source.local_path))
    {
        _watcher = std::make_unique<ZstdFileWatcher>(file_source.local_path);
        return;
    }

    if (_is_blf_source)
    {
        _watcher = std::make_unique<BlfFileWatcher>(file_source.local_path, std::move(notifier));
        return;
    }

    _watcher = create_log_watcher_for_source(file_source);
}

void TrackedSourceFile::try_initialize_timestamp_parser(const std::vector<std::string>& lines)
{
    if (_timestamp_parser_initialized)
    {
        return;
    }

    for (const auto& line : lines)
    {
        LogEntry probe(line);
        const auto catalog = timestamp_formats();
        if (catalog == nullptr || !_timestamp_parser.init(probe, *catalog))
        {
            continue;
        }

        _timestamp_parser_initialized = true;
        return;
    }
}

void TrackedSourceFile::add_entries_from_raw_strings(std::vector<std::string> lines)
{
    try_initialize_timestamp_parser(lines);

    reserve_entries(lines.size());
    int last_reported_percent = -1;
    if (_is_blf_source && !lines.empty())
    {
        report_content_parse_progress(0);
        last_reported_percent = 0;
    }

    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index)
    {
        auto& line       = lines[line_index];
        LogEntry& entry = append_entry();
        entry.text      = std::move(line);
        _timestamp_parser.parse(entry);
        (void)apply_timestamp_offset(entry);

        if (_is_blf_source && !lines.empty())
        {
            const int percent = rounded_progress_percent(line_index + 1, lines.size());
            if (percent != last_reported_percent)
            {
                report_content_parse_progress(percent);
                last_reported_percent = percent;
            }
        }
    }
}

void TrackedSourceFile::report_content_parse_progress(int percent)
{
    percent = std::max(0, std::min(100, percent));

    Notification notification;
    notification.title = "Parsing BLF content";
    notification.message = std::to_string(percent) + "% " + source_basename_for_progress(source());
    notification.level = percent >= 100 ? NotificationLevel::Success : NotificationLevel::Info;
    notification.progress = static_cast<float>(percent) / 100.0F;
    notification.timeout = percent >= 100 ? std::chrono::seconds(2) : std::chrono::milliseconds(0);
    (void)_content_parse_progress_notification.show_or_update(std::move(notification));
}

bool TrackedSourceFile::poll()
{
    if (_watcher == nullptr)
    {
        return false;
    }

    std::vector<std::string> lines;
    if (!_watcher->poll(lines) || lines.empty())
    {
        return false;
    }

    add_entries_from_raw_strings(std::move(lines));
    return true;
}

void TrackedSourceFile::set_timestamp_format(std::string format)
{
    set_timestamp_formats(std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {std::move(format)}));
    reparse_entries(_timestamp_parser, _timestamp_parser_initialized);
}

} // namespace slayerlog
