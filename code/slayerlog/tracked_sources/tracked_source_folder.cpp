#include "tracked_source_folder.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "log_batch.hpp"

namespace slayerlog
{

namespace
{

std::string normalize_file_path_for_key(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::path normalized_path = std::filesystem::weakly_canonical(path, error_code);
    if (error_code)
    {
        error_code.clear();
        normalized_path = std::filesystem::absolute(path, error_code);
        if (error_code)
        {
            normalized_path = path.lexically_normal();
        }
        else
        {
            normalized_path = normalized_path.lexically_normal();
        }
    }

    std::string normalized_text = normalized_path.make_preferred().string();
#ifdef _WIN32
    std::transform(normalized_text.begin(), normalized_text.end(), normalized_text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
    return normalized_text;
}

std::vector<std::filesystem::path> enumerate_regular_files(const std::string& folder_path)
{
    const std::filesystem::path path(folder_path);
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Folder does not exist: " + folder_path);
    }

    if (!std::filesystem::is_directory(path))
    {
        throw std::runtime_error("Path is not a folder: " + folder_path);
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.is_regular_file())
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) { return lhs.lexically_normal().generic_string() < rhs.lexically_normal().generic_string(); });
    return files;
}

} // namespace

TrackedSourceFolder::TrackedSourceFolder(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats)
    : TrackedSourceBase(std::move(source), std::move(source_label), std::move(timestamp_formats))
{
}

bool TrackedSourceFolder::poll()
{
    refresh_active_children();

    std::vector<LogBatchSourceRange> source_ranges;
    source_ranges.reserve(_active_file_order.size());
    for (std::size_t source_index = 0; source_index < _active_file_order.size(); ++source_index)
    {
        const std::string& path_key = _active_file_order[source_index];
        auto child_it               = _children.find(path_key);
        if (child_it == _children.end())
        {
            continue;
        }

        auto& child = child_it->second;
        TrackedSourceFile& child_source     = *child.tracked_source;
        const std::size_t first_new_entry_index = child_source.entries().size();
        if (!child_source.poll())
        {
            continue;
        }

        const auto& child_entries = child_source.entries();
        if (first_new_entry_index >= child_entries.size())
        {
            continue;
        }

        source_ranges.push_back({
            &child_entries,
            first_new_entry_index,
            source_index,
            source_label(),
        });
    }

    if (source_ranges.empty())
    {
        return false;
    }

    append_merged_entries(source_ranges);

    return true;
}

void TrackedSourceFolder::set_timestamp_format(std::string format)
{
    auto formats = std::make_shared<const TimestampFormatCatalog>(std::vector<std::string> {std::move(format)});
    set_timestamp_formats(formats);

    for (auto& child : _children)
    {
        if (child.second.tracked_source != nullptr)
        {
            child.second.tracked_source->set_timestamp_format(formats->formats().front());
        }
    }

    std::vector<LogBatchSourceRange> source_ranges;
    source_ranges.reserve(_active_file_order.size());
    for (std::size_t source_index = 0; source_index < _active_file_order.size(); ++source_index)
    {
        const auto child_it = _children.find(_active_file_order[source_index]);
        if (child_it == _children.end() || child_it->second.tracked_source == nullptr)
        {
            continue;
        }

        const auto& child_entries = child_it->second.tracked_source->entries();
        if (child_entries.empty())
        {
            continue;
        }

        source_ranges.push_back({
            &child_entries,
            0,
            source_index,
            source_label(),
        });
    }

    replace_entries_with_merged_entries(source_ranges);
}

void TrackedSourceFolder::refresh_active_children()
{
    _active_file_order.clear();
    _active_file_paths.clear();

    for (const auto& file_path : enumerate_regular_files(source().local_folder_path))
    {
        const std::string path_key = normalize_file_path_for_key(file_path);
        _active_file_order.push_back(path_key);
        _active_file_paths.insert(path_key);

        if (_children.find(path_key) != _children.end())
        {
            continue;
        }

        ChildState child;
        child.tracked_source = std::make_unique<TrackedSourceFile>(parse_log_source(file_path.string()), file_path.filename().string(), timestamp_formats());

        _children.emplace(path_key, std::move(child));
    }

    remove_inactive_children();
}

void TrackedSourceFolder::remove_inactive_children()
{
    for (auto child_it = _children.begin(); child_it != _children.end();)
    {
        if (_active_file_paths.find(child_it->first) != _active_file_paths.end())
        {
            ++child_it;
            continue;
        }

        child_it = _children.erase(child_it);
    }
}

} // namespace slayerlog
