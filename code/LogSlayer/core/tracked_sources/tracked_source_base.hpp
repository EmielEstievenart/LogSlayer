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
     * Pass an empty string to drop the display prefix entirely.
     */
    void set_source_mnemonic(std::string source_mnemonic);

    /**
     * @brief Returns whether the mnemonic is currently shown in presented text.
     */
    bool mnemonic_visible() const;

    /**
     * @brief Shows or hides the mnemonic prefix in presented text.
     *
     * The mnemonic only helps when several sources are interleaved, so callers
     * hide it for a lone source. Entries keep their raw text; toggling only
     * changes presentation.
     */
    void set_mnemonic_visible(bool mnemonic_visible);

    /**
     * @brief Returns the prefix added to a source's entries when presented.
     *
     * Empty when the source has no visible mnemonic. Otherwise it is the mnemonic
     * followed by a single space separator.
     */
    std::string mnemonic_prefix() const;

    /**
     * @brief Pins this source to a single timestamp format.
     *
     * The format is compiled once into a single-entry catalog and applied via
     * set_timestamp_catalog.
     */
    void set_timestamp_format(std::string format);

    /**
     * @brief Replaces the timestamp format catalog and reparses all entries.
     *
     * Passing the application-wide catalog restores automatic format detection.
     * Implementations are expected to reset their timestamp parser state, reparse
     * existing entries, and propagate the catalog to any child sources.
     */
    virtual void set_timestamp_catalog(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats) = 0;

    /**
     * @brief Sets an offset to apply to parsed timestamps.
     *
     * @return An error message if applying the offset fails; otherwise `std::nullopt`.
     */
    virtual std::optional<std::string> set_timestamp_offset(LogTimestampOffset offset);

    /**
     * @brief Adds @p delta on top of the currently configured timestamp offset.
     *
     * @return An error message if combining or applying the offset fails; otherwise `std::nullopt`.
     */
    std::optional<std::string> adjust_timestamp_offset(LogTimestampOffset delta);

    /**
     * @brief Clears the active timestamp offset from this source and its entries.
     */
    virtual void clear_timestamp_offset();

    /**
     * @brief Returns the currently configured timestamp offset, if any.
     */
    const std::optional<LogTimestampOffset>& timestamp_offset() const;

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
     * @brief Replaces the timestamp format catalog.
     *
     * If @p timestamp_formats is null, the default catalog is used instead.
     */
    void set_timestamp_formats(std::shared_ptr<const TimestampFormatCatalog> timestamp_formats);

    /**
     * @brief Stores the timestamp offset without touching the entries.
     *
     * For sources whose entries are rebuilt from children (folders): the offset only
     * needs to be recorded here, applying it to soon-to-be-replaced entries is wasted work.
     */
    void store_timestamp_offset(std::optional<LogTimestampOffset> offset);

    /**
     * @brief Clears and reparses timestamp metadata for all entries.
     *
     * Resets @p parser and re-runs format detection over the existing entries.
     */
    void reparse_entries(SourceTimestampParser& parser);

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
     * @brief Appends a new empty entry and assigns source metadata.
     */
    LogEntry& append_entry();

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
