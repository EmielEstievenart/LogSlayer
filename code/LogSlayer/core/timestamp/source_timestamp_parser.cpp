#include "timestamp/source_timestamp_parser.hpp"

#include <algorithm>

#include "eestv/timestamp/timestamp_parser.hpp"

namespace slayerlog
{

namespace
{

using eestv::DateAndTime;
using eestv::TimestampParser;

// Probing is bounded so that pathological input stays cheap: only the first few token
// positions of a line are considered, and detection stops once a handful of lines have
// produced a match.
constexpr int max_probe_start_indices   = 8;
constexpr std::size_t max_quorum_probes = 8;

struct ParsedMatch
{
    LogTimestamp timestamp;
    int end_index = 0;
};

std::optional<ParsedMatch> run_parser(const eestv::CompiledDateAndTimeParser& parser, std::string_view line, int start_index)
{
    if (parser.steps.empty())
    {
        return std::nullopt;
    }

    DateAndTime parsed;
    int index = start_index;
    for (const auto& step : parser.steps)
    {
        int index_jump = 0;
        if (!step(line, index, index_jump, parsed))
        {
            return std::nullopt;
        }

        index += index_jump;
    }

    const auto timestamp = make_log_timestamp_utc(parsed.year, parsed.month, parsed.day, parsed.hour, parsed.minute, parsed.second, parsed.nanosecond, parsed.utc_offset_minutes);
    if (!timestamp.has_value())
    {
        return std::nullopt;
    }

    return ParsedMatch {*timestamp, index};
}

struct LineCandidate
{
    std::size_t entry_index  = 0;
    std::size_t slot         = 0;
    std::size_t match_length = 0;
};

// The candidate for one line: the earliest token position with any match, and the
// longest-matching format at that position.
std::optional<LineCandidate> probe_line(std::string_view line, const TimestampFormatCatalog& catalog)
{
    const auto start_indices = TimestampParser::possible_parse_start_indices(line, max_probe_start_indices);
    const auto& entries      = catalog.entries();

    for (std::size_t slot = 0; slot < start_indices.size(); ++slot)
    {
        const int start_index = start_indices[slot];
        std::optional<LineCandidate> best_match;
        for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index)
        {
            if (entries[entry_index].compiled_parser == nullptr)
            {
                continue;
            }

            const auto match = run_parser(*entries[entry_index].compiled_parser, line, start_index);
            if (!match.has_value())
            {
                continue;
            }

            const std::size_t match_length = static_cast<std::size_t>(match->end_index - start_index);
            if (!best_match.has_value() || match_length > best_match->match_length)
            {
                best_match = LineCandidate {entry_index, slot, match_length};
            }
        }

        if (best_match.has_value())
        {
            return best_match;
        }
    }

    return std::nullopt;
}

} // namespace

bool SourceTimestampParser::init(const LogEntry& line, const TimestampFormatCatalog& catalog)
{
    return init_from_lines([&line](std::size_t) -> std::string_view { return line.text; }, 1, catalog);
}

bool SourceTimestampParser::init(const std::vector<std::string>& lines, const TimestampFormatCatalog& catalog)
{
    return init_from_lines([&lines](std::size_t line_index) -> std::string_view { return lines[line_index]; }, lines.size(), catalog);
}

bool SourceTimestampParser::init(const std::vector<std::shared_ptr<LogEntry>>& entries, const TimestampFormatCatalog& catalog)
{
    return init_from_lines(
        [&entries](std::size_t entry_index) -> std::string_view
        {
            const auto& entry = entries[entry_index];
            return entry != nullptr ? std::string_view(entry->text) : std::string_view();
        },
        entries.size(), catalog);
}

bool SourceTimestampParser::initialized() const
{
    return _compiled_parser != nullptr && _detected_start_index_slot.has_value();
}

bool SourceTimestampParser::init_from_lines(const std::function<std::string_view(std::size_t)>& line_at, std::size_t line_count, const TimestampFormatCatalog& catalog)
{
    if (initialized())
    {
        return true;
    }

    struct CandidateVotes
    {
        LineCandidate candidate;
        std::size_t votes = 0;
    };

    std::vector<CandidateVotes> tallies;
    std::size_t probed_matches = 0;

    for (std::size_t line_index = 0; line_index < line_count && probed_matches < max_quorum_probes; ++line_index)
    {
        const auto candidate = probe_line(line_at(line_index), catalog);
        if (!candidate.has_value())
        {
            continue;
        }

        ++probed_matches;
        const auto tally = std::find_if(tallies.begin(), tallies.end(), [&candidate](const CandidateVotes& votes) { return votes.candidate.entry_index == candidate->entry_index && votes.candidate.slot == candidate->slot; });
        if (tally == tallies.end())
        {
            tallies.push_back(CandidateVotes {*candidate, 1});
        }
        else
        {
            ++tally->votes;
            tally->candidate.match_length = std::max(tally->candidate.match_length, candidate->match_length);
        }
    }

    if (tallies.empty())
    {
        return false;
    }

    const auto is_better = [](const CandidateVotes& lhs, const CandidateVotes& rhs)
    {
        if (lhs.votes != rhs.votes)
        {
            return lhs.votes > rhs.votes;
        }
        if (lhs.candidate.slot != rhs.candidate.slot)
        {
            return lhs.candidate.slot < rhs.candidate.slot;
        }
        if (lhs.candidate.match_length != rhs.candidate.match_length)
        {
            return lhs.candidate.match_length > rhs.candidate.match_length;
        }
        return lhs.candidate.entry_index < rhs.candidate.entry_index;
    };

    const CandidateVotes* winner = &tallies.front();
    for (const auto& tally : tallies)
    {
        if (is_better(tally, *winner))
        {
            winner = &tally;
        }
    }

    _compiled_parser           = catalog.entries()[winner->candidate.entry_index].compiled_parser;
    _detected_start_index_slot = winner->candidate.slot;
    return true;
}

bool SourceTimestampParser::parse(LogEntry& line) const
{
    if (!initialized())
    {
        return false;
    }

    const int start_index = TimestampParser::nth_parse_start_index(line.text, static_cast<int>(*_detected_start_index_slot));
    if (start_index < 0)
    {
        return false;
    }

    const auto match = run_parser(*_compiled_parser, line.text, start_index);
    if (!match.has_value())
    {
        return false;
    }

    line.metadata.timestamp            = match->timestamp;
    line.metadata.extracted_time_start = static_cast<std::size_t>(start_index);
    line.metadata.extracted_time_end   = static_cast<std::size_t>(match->end_index);
    return true;
}

} // namespace slayerlog
