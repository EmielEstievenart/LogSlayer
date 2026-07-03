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

    return files;
}

void sort_files(std::vector<std::filesystem::path>& files)
{
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) { return lhs.lexically_normal().generic_string() < rhs.lexically_normal().generic_string(); });
}

Notification folder_open_progress_notification(std::string message, float progress)
{
    Notification notification = make_progress_notification("Opening folder", std::move(message), progress);

    // Reaching 100% opened is not the end of the open operation: the opener still
    // adopts the source and reloads the view (which can take a while) before it
    // calls finish_open_notification. Keep the notification sticky until then.
    notification.dismiss_when_done = false;
    return notification;
}

Notification folder_open_progress_notification(std::size_t opened_file_count, std::size_t total_file_count)
{
    const float progress = total_file_count == 0 ? 1.0F : static_cast<float>(opened_file_count) / static_cast<float>(total_file_count);
    return folder_open_progress_notification(std::to_string(opened_file_count) + " / " + std::to_string(total_file_count) + " files opened", progress);
}

} // namespace

TrackedSourceFolder::TrackedSourceFolder(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats, Notifier notifier)
    : TrackedSourceBase(std::move(source), std::move(source_label), std::move(timestamp_formats)), _open_progress_notification(std::move(notifier))
{
}

bool TrackedSourceFolder::poll()
{
    if (_report_open_progress)
    {
        // Covers enumerating and sorting the folder plus creating the per-file children.
        _open_progress_notification.show_or_update(folder_open_progress_notification("Scanning folder", 0.0F));
    }

    refresh_active_children();

    const std::size_t total_file_count = _active_file_order.size();
    std::size_t opened_file_count      = 0;
    if (_report_open_progress)
    {
        _open_progress_notification.show_or_update(folder_open_progress_notification(opened_file_count, total_file_count));
    }

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

        auto& child                             = child_it->second;
        TrackedSourceFile& child_source         = *child.tracked_source;
        const std::size_t first_new_entry_index = child_source.entries().size();
        const bool has_new_entries              = child_source.poll();
        if (_report_open_progress)
        {
            _open_progress_notification.show_or_update(folder_open_progress_notification(++opened_file_count, total_file_count));
        }

        if (!has_new_entries)
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

    _report_open_progress = false;

    if (source_ranges.empty())
    {
        return false;
    }

    append_merged_entries(source_ranges);

    return true;
}

void TrackedSourceFolder::finish_open_notification(std::string title, std::string message, NotificationLevel level)
{
    Notification notification;
    notification.title   = std::move(title);
    notification.message = std::move(message);
    notification.level   = level;
    if (level == NotificationLevel::Error)
    {
        notification.timeout = error_notification_timeout;
    }

    _open_progress_notification.show_or_update(std::move(notification));
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
            if (timestamp_offset().has_value())
            {
                (void)child.second.tracked_source->set_timestamp_offset(*timestamp_offset());
            }
        }
    }

    rebuild_entries_from_children();
}

std::optional<std::string> TrackedSourceFolder::set_timestamp_offset(LogTimestampOffset offset)
{
    const auto base_error = TrackedSourceBase::set_timestamp_offset(offset);
    if (base_error.has_value())
    {
        return base_error;
    }

    for (auto& child : _children)
    {
        if (child.second.tracked_source == nullptr)
        {
            continue;
        }

        const auto child_error = child.second.tracked_source->set_timestamp_offset(offset);
        if (child_error.has_value())
        {
            return child_error;
        }
    }

    rebuild_entries_from_children();
    return std::nullopt;
}

void TrackedSourceFolder::clear_timestamp_offset()
{
    TrackedSourceBase::clear_timestamp_offset();
    for (auto& child : _children)
    {
        if (child.second.tracked_source != nullptr)
        {
            child.second.tracked_source->clear_timestamp_offset();
        }
    }

    rebuild_entries_from_children();
}

void TrackedSourceFolder::rebuild_entries_from_children()
{
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

    auto files = enumerate_regular_files(source().local_folder_path);
    sort_files(files);

    for (const auto& file_path : files)
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
        if (timestamp_offset().has_value())
        {
            (void)child.tracked_source->set_timestamp_offset(*timestamp_offset());
        }

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
