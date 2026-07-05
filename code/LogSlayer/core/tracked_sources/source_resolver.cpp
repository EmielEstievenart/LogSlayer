#include "tracked_sources/source_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "log_source.hpp"
#include "tracked_sources/all_tracked_sources.hpp"

namespace slayerlog
{

namespace
{

std::string trim_text(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

std::string lowercase(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

/// Identity strings the reference could denote: a file/SSH source and, when the
/// path exists as a directory, a folder source. Parse failures yield no
/// candidates rather than errors — the reference may still match a mnemonic.
std::vector<std::string> candidate_identities(const std::string& reference)
{
    std::vector<std::string> identities;

    try
    {
        identities.push_back(source_identity(parse_log_source(reference)));
    }
    catch (const std::exception&)
    {
    }

    std::error_code error_code;
    if (std::filesystem::is_directory(reference, error_code))
    {
        try
        {
            identities.push_back(source_identity(make_local_folder_source(reference)));
        }
        catch (const std::exception&)
        {
        }
    }

    return identities;
}

} // namespace

std::optional<std::size_t> resolve_source_index(const AllTrackedSources& tracked_sources, std::string_view reference)
{
    const std::string trimmed_reference = trim_text(reference);
    if (trimmed_reference.empty())
    {
        return std::nullopt;
    }

    const std::string lowered_reference = lowercase(trimmed_reference);
    const auto mnemonics                = tracked_sources.source_mnemonics();
    for (std::size_t source_index = 0; source_index < mnemonics.size(); ++source_index)
    {
        // Stored mnemonics are space-padded to a fixed column width.
        const std::string mnemonic = trim_text(mnemonics[source_index]);
        if (!mnemonic.empty() && lowercase(mnemonic) == lowered_reference)
        {
            return source_index;
        }
    }

    const auto identities = candidate_identities(trimmed_reference);
    for (std::size_t source_index = 0; source_index < tracked_sources.source_count(); ++source_index)
    {
        const LogSource& source = tracked_sources.source_at(source_index);
        if (source.spec == trimmed_reference)
        {
            return source_index;
        }

        const std::string open_identity = source_identity(source);
        if (std::find(identities.begin(), identities.end(), open_identity) != identities.end())
        {
            return source_index;
        }
    }

    return std::nullopt;
}

} // namespace slayerlog
