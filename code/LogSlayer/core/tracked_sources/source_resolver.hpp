#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace slayerlog
{

class AllTrackedSources;

/**
 * @brief Resolves a textual source reference to an open source index.
 *
 * Accepts the path/URI form the source was opened with (matched through the
 * normalized source identity, so relative and absolute spellings of the same
 * file match) or a source mnemonic (case-insensitive). Returns std::nullopt
 * when nothing matches.
 */
std::optional<std::size_t> resolve_source_index(const AllTrackedSources& tracked_sources, std::string_view reference);

} // namespace slayerlog
