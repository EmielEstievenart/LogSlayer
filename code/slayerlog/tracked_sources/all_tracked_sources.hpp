#pragma once

#include <cstddef>
#include <memory>
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

/**
 * Owns all open log sources and maintains a single timestamp-merged view of
 * their lines (_all_lines). Sources are addressed by index; indices shift
 * when a source is closed.
 *
 * Methods returning std::optional<std::string> follow the convention:
 * std::nullopt on success, error message on failure.
 */
class AllTrackedSources
{
public:
    /**
     * @param timestamp_formats Catalog of timestamp formats available to sources.
     *                          Falls back to the default catalog if nullptr.
     */
    explicit AllTrackedSources(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());
    ~AllTrackedSources();

    /**
     * Adopts an already-constructed, already-polled (and already-tagged) source,
     * then rebuilds labels and the merged line view. Fails if it is nullptr or
     * its source is already open.
     *
     * Mnemonic selection is the factory's job: use adopt_opened_source() (or the
     * open_source() create + poll + adopt helper) so the source is tagged before
     * it reaches here.
     */
    std::optional<std::string> add_opened_source(std::unique_ptr<TrackedSourceBase> source_state);

    /**
     * Closes the source at @p source_index. Indices of subsequent sources
     * shift down by one.
     *
     * @param closed_label If non-null, receives the label of the closed source.
     */
    std::optional<std::string> close_source(std::size_t source_index, std::string* closed_label = nullptr);

    /**
     * Polls all sources for new entries and merges them into the line view.
     * New entries are appended when possible; if they are older than the
     * current tail, the affected suffix is re-merged in place.
     *
     * Per-source poll exceptions are logged and skipped.
     *
     * @return Index of the first changed/added line, or std::nullopt if
     *         nothing changed.
     */
    std::optional<AllLineIndex> poll();

    /** Merged, timestamp-ordered lines across all sources. */
    const IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex>& all_lines() const;
    int line_count() const;

    /** Character length of the longest line seen so far (for layout/scrolling). */
    int widest_line_width() const;

    std::size_t source_count() const;
    bool empty() const;
    bool is_source_open(const LogSource& candidate_source) const;
    std::vector<std::string> source_labels() const;
    std::vector<std::string> source_mnemonics() const;

    /** Labels formatted as "mnemonic — label", or just the label if no mnemonic. */
    std::vector<std::string> source_display_labels() const;

    const std::vector<std::string>& timestamp_formats() const;
    std::shared_ptr<const TimestampFormatCatalog> timestamp_format_catalog() const;

    /** Notifier used for progress notifications during full rebuilds. */
    void set_notifier(Notifier notifier);

    /**
     * Sets the timestamp format of one source and rebuilds the merged view.
     * @p format must be non-empty.
     */
    std::optional<std::string> set_source_timestamp_format(std::size_t source_index, const std::string& format);

    /** Applies a timestamp offset to one source and rebuilds the merged view. */
    std::optional<std::string> set_source_timestamp_offset(std::size_t source_index, LogTimestampOffset offset);

    /** Removes the timestamp offset of one source and rebuilds the merged view. */
    std::optional<std::string> clear_source_timestamp_offset(std::size_t source_index);

private:
    void rebuild_source_labels();

    /** Shows mnemonics only while more than one source is open, hiding them for a lone source. */
    void update_mnemonic_visibility();

    /** Re-merges all entries from all sources from scratch, reporting progress via _notifier. */
    void rebuild_all_lines();

    void update_widest_line_width(const std::shared_ptr<LogEntry>& line);
    void append_source_range(std::vector<LogBatchSourceRange>& source_ranges, const TrackedSourceBase& source, std::size_t source_index, std::size_t first_entry_index) const;
    void append_merged_lines(const std::vector<std::shared_ptr<LogEntry>>& lines);

    std::vector<std::unique_ptr<TrackedSourceBase>> _sources;
    IndexedVector<std::shared_ptr<LogEntry>, AllLineIndex> _all_lines;
    int _widest_line_width = 0;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
    Notifier _notifier;
};

} // namespace slayerlog