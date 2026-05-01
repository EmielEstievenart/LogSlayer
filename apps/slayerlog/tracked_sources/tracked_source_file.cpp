#include "tracked_source_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

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

} // namespace

TrackedSourceFile::TrackedSourceFile(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats) : TrackedSourceBase(std::move(source), std::move(source_label), std::move(timestamp_formats))
{
    const LogSource& file_source = this->source();
    if (file_source.kind == LogSourceKind::LocalFile && has_zstd_extension(file_source.local_path))
    {
        _watcher = std::make_unique<ZstdFileWatcher>(file_source.local_path);
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
    for (auto& line : lines)
    {
        LogEntry& entry = append_entry();
        entry.text      = std::move(line);
        _timestamp_parser.parse(entry);
    }
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

} // namespace slayerlog
