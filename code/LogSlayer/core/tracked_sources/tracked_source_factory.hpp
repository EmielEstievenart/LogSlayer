#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "tracked_source_folder.hpp"

#include "tracked_source_base.hpp"

namespace slayerlog
{

class AllTrackedSources;

std::unique_ptr<TrackedSourceBase> create_tracked_source(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats = default_timestamp_format_catalog(), Notifier notifier = {});

/**
 * @brief Picks a stable, unique mnemonic for a source.
 *
 * Maps the source identity onto a curated mountain name via a deterministic
 * hash, then probes forward past any name already in @p mnemonics_in_use so the
 * result is unique among the currently-open sources. Falls back to a numbered
 * name when every mountain is taken.
 */
std::string pick_unique_mnemonic(const LogSource& source, const std::unordered_set<std::string>& mnemonics_in_use);

/**
 * @brief Assigns a mnemonic to @p source_state and adopts it into @p sources.
 *
 * The factory owns mnemonic selection: this picks a stable, unique mnemonic
 * against the sources already open in @p sources, then hands the (now-tagged)
 * source to AllTrackedSources::add_opened_source. Callers must hold the model
 * lock so the uniqueness check sees a stable source set. Returns std::nullopt on
 * success or an error message on failure.
 */
std::optional<std::string> adopt_opened_source(AllTrackedSources& sources, std::unique_ptr<TrackedSourceBase> source_state);

/**
 * @brief Creates, primes, and adopts a source into @p sources in one call.
 *
 * Convenience for synchronous callers: builds the tracked source, polls it
 * once, then hands it to adopt_opened_source. Returns std::nullopt on success or
 * an error message on failure.
 */
std::optional<std::string> open_source(AllTrackedSources& sources, const LogSource& source, Notifier notifier = {});

} // namespace slayerlog
