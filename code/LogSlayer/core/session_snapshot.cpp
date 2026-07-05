#include "session_snapshot.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>

#include "log_source.hpp"
#include "log_view_service.hpp"
#include "timestamp/log_timestamp.hpp"
#include "commands/command_arguments.hpp"
#include "tracked_sources/all_processed_sources.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

std::string absolutized(const std::string& path)
{
    std::error_code error_code;
    const auto absolute_path = std::filesystem::absolute(path, error_code);
    if (error_code)
    {
        return path;
    }

    return absolute_path.lexically_normal().string();
}

/// The reference emitted both as the open argument and as the source reference
/// of set-offset/set-time-format lines. Local paths are absolutized so the
/// replay works from any working directory; SSH specs are already location-free.
std::string source_reference(const LogSource& source)
{
    switch (source.kind)
    {
    case LogSourceKind::LocalFolder:
        return absolutized(source.local_folder_path);
    case LogSourceKind::SshRemoteFile:
        return source.spec;
    case LogSourceKind::LocalFile:
    default:
        return absolutized(source.local_path);
    }
}

/// The raw line number of the visible row at the viewport center, stepping
/// outward past rows without one (collapsed identical-line summaries).
std::optional<int> center_line_number(const AllProcessedSources& processed_sources, const LogViewService& log_view)
{
    const int line_count = processed_sources.line_count();
    if (line_count <= 0 || log_view.viewport_line_count() <= 0)
    {
        return std::nullopt;
    }

    const int center_row = std::clamp(log_view.first_visible_line() + log_view.viewport_line_count() / 2, 0, line_count - 1);
    for (int distance = 0; distance < line_count; ++distance)
    {
        for (const int row : {center_row - distance, center_row + distance})
        {
            if (row < 0 || row >= line_count)
            {
                continue;
            }

            const auto line_number = processed_sources.line_number_for_visible_line(VisibleLineIndex {row});
            if (line_number.has_value())
            {
                return line_number;
            }
        }
    }

    return std::nullopt;
}

} // namespace

std::vector<std::string> serialize_session_commands(const AllTrackedSources& tracked_sources, const AllProcessedSources& processed_sources, const LogViewService* log_view)
{
    std::vector<std::string> commands;

    std::vector<std::string> references;
    references.reserve(tracked_sources.source_count());
    for (std::size_t source_index = 0; source_index < tracked_sources.source_count(); ++source_index)
    {
        references.push_back(source_reference(tracked_sources.source_at(source_index)));
        commands.push_back("open " + references.back());
    }

    for (std::size_t source_index = 0; source_index < tracked_sources.source_count(); ++source_index)
    {
        const auto format_override = tracked_sources.source_timestamp_format_override(source_index);
        if (format_override.has_value())
        {
            commands.push_back("set-time-format " + quote_command_argument(references[source_index]) + " " + *format_override);
        }
    }

    for (std::size_t source_index = 0; source_index < tracked_sources.source_count(); ++source_index)
    {
        const auto offset = tracked_sources.source_timestamp_offset(source_index);
        if (offset.has_value())
        {
            commands.push_back("set-offset " + quote_command_argument(references[source_index]) + " " + serialize_log_timestamp_offset(*offset));
        }
    }

    for (const auto& filter_text : processed_sources.include_filters())
    {
        commands.push_back("filter-in " + filter_text);
    }
    for (const auto& filter_text : processed_sources.exclude_filters())
    {
        commands.push_back("filter-out " + filter_text);
    }

    const auto hidden_columns = processed_sources.hidden_columns();
    if (hidden_columns.has_value())
    {
        commands.push_back("hide-columns " + std::to_string(hidden_columns->start) + "-" + std::to_string(hidden_columns->end));
    }

    commands.push_back(processed_sources.hide_identical_lines() ? "hide-identical-lines" : "show-identical-lines");
    commands.push_back(processed_sources.show_original_time() ? "show-original-time" : "hide-original-time");

    const auto hidden_before = processed_sources.hidden_before_line_number();
    if (hidden_before.has_value())
    {
        commands.push_back("hide-before-line " + std::to_string(*hidden_before));
    }

    if (log_view != nullptr)
    {
        const auto line_number = center_line_number(processed_sources, *log_view);
        if (line_number.has_value())
        {
            commands.push_back("go-to-line " + std::to_string(*line_number));
        }
    }

    return commands;
}

} // namespace slayerlog
