#include "tracked_source_factory.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <string_view>
#include <utility>

#include "log_source.hpp"
#include "tracked_sources/all_tracked_sources.hpp"
#include "tracked_source_file.hpp"
#include "tracked_source_folder.hpp"

namespace slayerlog
{

namespace
{

// Deterministic 64-bit FNV-1a hash. Used to map a source identity onto a mnemonic so that the
// same source always receives the same name, independent of platform or std::hash seeding.
std::uint64_t stable_identity_hash(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text)
    {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }

    return hash;
}

// Curated list of recognizable mountains, all single ASCII tokens to keep the source column tidy.
// Each open log source is assigned one of these as a stable, searchable tag.
constexpr std::array<std::string_view, 96> mountain_mnemonics = {
    "Everest    ", "Denali     ", "Elbrus     ", "Aconcagua  ", "Lhotse     ", "Makalu     ", "Kilimanjaro", "Vinson     ", "Matterhorn ", "Eiger      ", "Jungfrau   ", "Monch      ", "Fuji       ", "Etna       ",
    "Olympus    ", "Rainier    ", "Shasta     ", "Hood       ", "Whitney    ", "Baker      ", "Adams      ", "Jefferson  ", "Lassen     ", "Ararat     ", "Cook       ", "Logan      ", "Robson     ", "Teide      ",
    "Snowdon    ", "Nevis      ", "Scafell    ", "Hekla      ", "Erebus     ", "Tambora    ", "Krakatoa   ", "Vesuvius   ", "Cotopaxi   ", "Chimborazo ", "Washington ", "Mitchell   ", "Marcy      ", "Katahdin   ",
    "Greylock   ", "Elbert     ", "Harvard    ", "Princeton  ", "Yale       ", "Columbia   ", "Lincoln    ", "Sherman    ", "Antero     ", "Shavano    ", "Sneffels   ", "Wilson     ", "Eolus      ", "Massive    ",
    "Bierstadt  ", "Evans      ", "Longs      ", "Pikes      ", "Bross      ", "Belford    ", "Oxford     ", "Quandary   ", "Castle     ", "Maroon     ", "Pyramid    ", "Humboldt   ", "Redcloud   ", "Handies    ",
    "Grays      ", "Torreys    ", "Wrangell   ", "Bona       ", "Blackburn  ", "Sanford    ", "Hunter     ", "Foraker    ", "Toubkal    ", "Damavand   ", "Kenya      ", "Stanley    ", "Karisimbi  ", "Wilhelm    ",
    "Ruapehu    ", "Taranaki   ", "Tasman     ", "Aoraki     ", "Kinabalu   ", "Apo        ", "Annapurna  ", "Dhaulagiri ", "Manaslu    ", "Rakaposhi  ", "Kazbek     ", "Bernina    ",
};

} // namespace

std::unique_ptr<TrackedSourceBase> create_tracked_source(LogSource source, std::string source_label, std::shared_ptr<const TimestampFormatCatalog> timestamp_formats, Notifier notifier)
{
    if (source.kind == LogSourceKind::LocalFolder)
    {
        return std::make_unique<TrackedSourceFolder>(std::move(source), std::move(source_label), std::move(timestamp_formats), std::move(notifier));
    }

    // Single-file sources report no open progress of their own; the open-file
    // command wraps the whole open in its own progress notification.
    return std::make_unique<TrackedSourceFile>(std::move(source), std::move(source_label), std::move(timestamp_formats));
}

std::string pick_unique_mnemonic(const LogSource& source, const std::unordered_set<std::string>& mnemonics_in_use)
{
    const std::uint64_t base_index = stable_identity_hash(source_identity(source)) % mountain_mnemonics.size();

    for (std::size_t probe = 0; probe < mountain_mnemonics.size(); ++probe)
    {
        const std::size_t candidate_index = static_cast<std::size_t>((base_index + probe) % mountain_mnemonics.size());
        std::string candidate(mountain_mnemonics[candidate_index]);
        if (mnemonics_in_use.count(candidate) == 0)
        {
            return candidate;
        }
    }

    // More open sources than available mnemonics: fall back to a numbered name that stays unique.
    std::string fallback;
    for (std::size_t suffix = mnemonics_in_use.size();; ++suffix)
    {
        fallback = std::string(mountain_mnemonics[static_cast<std::size_t>(base_index)]) + std::to_string(suffix);
        if (mnemonics_in_use.count(fallback) == 0)
        {
            return fallback;
        }
    }
}

std::optional<std::string> adopt_opened_source(AllTrackedSources& sources, std::unique_ptr<TrackedSourceBase> source_state)
{
    if (source_state == nullptr)
    {
        return "Opened source is invalid";
    }

    // Mnemonic selection lives here, not in AllTrackedSources: tag the source with a stable,
    // unique name before it joins the model. Existing sources keep theirs, so opening or closing
    // other sources never disturbs an already-assigned mnemonic.
    std::unordered_set<std::string> mnemonics_in_use;
    for (const auto& mnemonic : sources.source_mnemonics())
    {
        if (!mnemonic.empty())
        {
            mnemonics_in_use.insert(mnemonic);
        }
    }
    source_state->set_source_mnemonic(pick_unique_mnemonic(source_state->source(), mnemonics_in_use));

    return sources.add_opened_source(std::move(source_state));
}

std::optional<std::string> open_source(AllTrackedSources& sources, const LogSource& source, Notifier notifier)
{
    if (sources.is_source_open(source))
    {
        return "Source already open: " + source_display_path(source);
    }

    try
    {
        auto source_state = create_tracked_source(source, source_display_path(source), sources.timestamp_format_catalog(), std::move(notifier));

        // Watchers bound the bytes they ingest per poll; opening drains the whole
        // existing content so the source is complete before it joins the model.
        do
        {
            source_state->poll();
        } while (source_state->backlog_pending());

        return adopt_opened_source(sources, std::move(source_state));
    }
    catch (const std::exception& ex)
    {
        return ex.what();
    }
}

} // namespace slayerlog
