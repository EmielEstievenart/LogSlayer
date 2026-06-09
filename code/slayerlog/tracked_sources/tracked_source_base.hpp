#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "log_source.hpp"
#include "log_line.hpp"
#include "timestamp/source_timestamp_parser.hpp"

namespace slayerlog
{

struct LogBatchSourceRange;

/**
 * @brief Base class for a tracked log source.
 *
 * Owns the parsed log entries for one source, source labeling metadata,
 * timestamp parsing configuration, and optional timestamp offset handling.
 *
 * Derived classes provide the concrete polling/loading behavior for specific
 * source types, such as local files, folders, or SSH sources.
 */
class TrackedSourceBase
{
public:
    /**
     * @brief Creates a tracked source.
     *
     * If @p timestamp_formats is null, the default timestamp format catalog is used.
     */
    TrackedSourceBase(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog());

    virtual ~TrackedSourceBase() = default;

    /**
     * @brief Returns the source represented by this tracker.
     */
    const LogSource& source() const;

    /**
     * @brief Returns the current display label for this source.
     */
    const std::string& source_label() const;

    /**
     * @brief Updates the display label for this source.
     */
    void set_source_label(std::string source_label);

    /**
     * @brief Returns the stable mnemonic assigned to this source.
     */
    const std::string& source_mnemonic() const;

    /**
     * @brief Updates the stable mnemonic assigned to this source.
     *
     * The mnemonic is embedded as a fixed prefix at the start of every entry's
     * text, so changing it rewrites the prefix on all existing entries (and any
     * extracted-timestamp offsets shift to match). Pass an empty string to drop
     * the prefix entirely.
     */
    void set_source_mnemonic(std::string source_mnemonic);

    /**
     * @brief Returns whether the mnemonic is currently embedded in entry text.
     */
    bool mnemonic_visible() const;

    /**
     * @brief Shows or hides the embedded mnemonic prefix.
     *
     * The mnemonic only helps when several sources are interleaved, so callers
     * hide it for a lone source. Toggling rewrites the prefix on every existing
     * entry (shifting extracted-timestamp offsets to match); newly ingested lines
     * follow the current visibility.
     */
    void set_mnemonic_visible(bool mnemonic_visible);

    /**
     * @brief Sets the timestamp format used by this source.
     *
     * Implementations are expected to update their timestamp parser state and
     * reparse existing entries where needed.
     */
    virtual void set_timestamp_format(std::string format) = 0;

    /**
     * @brief Sets an offset to apply to parsed timestamps.
     *
     * @return An error message if applying the offset fails; otherwise `std::nullopt`.
     */
    virtual std::optional<std::string> set_timestamp_offset(LogTimestampOffset offset);

    /**
     * @brief Clears the active timestamp offset from this source and its entries.
     */
    virtual void clear_timestamp_offset();

    /**
     * @brief Polls the source for new or changed log entries.
     *
     * @return `true` if the source changed; otherwise `false`.
     */
    virtual bool poll() = 0;

    /**
     * @brief Returns all entries currently tracked for this source.
     */
    const std::vector<std::shared_ptr<LogEntry>>& entries() const;

protected:
    /**
     * @brief Returns the timestamp format catalog used by this source.
     */
    const std::shared_ptr<const TimestampFormatCatalog>& timestamp_formats() const;

    /**
     * @brief Returns the currently configured timestamp offset, if any.
     */
    const std::optional<LogTimestampOffset>& timestamp_offset() const;

    /**
     * @brief Replaces the timestamp format catalog.
     *
     * If @p timestamp_formats is null, the default catalog is used instead.
     */
    void set_timestamp_formats(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats);

    /**
     * @brief Clears and reparses timestamp metadata for all entries.
     */
    void reparse_entries(SourceTimestampParser& parser, bool& parser_initialized);

    /**
     * @brief Applies the current timestamp offset to all existing entries.
     *
     * @return An error message if any offset would overflow; otherwise `std::nullopt`.
     */
    std::optional<std::string> apply_timestamp_offset_to_entries();

    /**
     * @brief Applies the current timestamp offset to a single entry.
     *
     * @return An error message if the offset would overflow; otherwise `std::nullopt`.
     */
    std::optional<std::string> apply_timestamp_offset(LogEntry& entry) const;

    /**
     * @brief Reserves storage for additional entries.
     */
    void reserve_entries(std::size_t additional_count);

    /**
     * @brief Returns the prefix embedded at the start of every entry's text.
     *
     * Empty when the source has no mnemonic. Otherwise it is the mnemonic
     * followed by a single space separator.
     */
    std::string mnemonic_prefix() const;

    /**
     * @brief Embeds the mnemonic prefix at the start of @p entry.
     *
     * Shifts any extracted-timestamp offsets by the prefix length. A no-op when
     * the source has no mnemonic. Callers must inject only entries whose text is
     * still in raw (prefix-free) form, for example a freshly parsed line.
     */
    void inject_mnemonic_prefix(LogEntry& entry) const;

    /**
     * @brief Appends a new empty entry and assigns source metadata.
     */
    LogEntry& append_entry();

    /**
     * @brief Replaces the @p old_prefix with @p new_prefix on every entry.
     *
     * Used when the mnemonic or its visibility changes. Shifts extracted-timestamp
     * offsets to match; a no-op when the prefixes are identical.
     */
    void rewrite_prefix_on_entries(const std::string& old_prefix, const std::string& new_prefix);

    /**
     * @brief Appends merged entries from batch source ranges.
     *
     * Newly appended entries receive sequence numbers and are linked back to this source.
     */
    void append_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges);

    /**
     * @brief Replaces all entries with merged entries from batch source ranges.
     */
    void replace_entries_with_merged_entries(const std::vector<LogBatchSourceRange>& source_ranges);

private:
    LogSource _source;
    std::string _source_label;
    std::string _source_mnemonic;
    bool _mnemonic_visible = false;
    std::vector<std::shared_ptr<LogEntry>> _entries;
    std::uint64_t _next_sequence_number = 0;
    std::shared_ptr<const TimestampFormatCatalog> _timestamp_formats;
    std::optional<LogTimestampOffset> _timestamp_offset;
};

} // namespace slayerlog