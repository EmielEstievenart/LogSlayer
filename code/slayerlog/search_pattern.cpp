#include "search_pattern.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>

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

} // namespace

SearchPattern compile_search_pattern(std::string_view text)
{
    const std::string trimmed_text = trim_search_text(text);
    if (trimmed_text.empty())
    {
        throw std::invalid_argument("Search text must not be empty");
    }

    constexpr std::string_view regex_prefix {"re:"};
    if (trimmed_text.rfind(regex_prefix.data(), 0) != 0)
    {
        return SearchPattern {
            trimmed_text,
            trimmed_text,
            std::nullopt,
        };
    }

    std::string regex_text = trimmed_text.substr(regex_prefix.size());
    if (regex_text.empty())
    {
        throw std::invalid_argument("Regex pattern after re: must not be empty");
    }

    try
    {
        return SearchPattern {
            trimmed_text,
            regex_text,
            std::regex(regex_text),
        };
    }
    catch (const std::regex_error& error)
    {
        throw std::invalid_argument("Invalid regex: " + std::string(error.what()));
    }
}

bool matches_pattern(std::string_view haystack, const SearchPattern& pattern)
{
    if (!pattern.regex.has_value())
    {
        return haystack.find(pattern.needle) != std::string_view::npos;
    }

    return std::regex_search(haystack.begin(), haystack.end(), *pattern.regex);
}

bool matches_any_pattern(std::string_view haystack, const std::vector<SearchPattern>& patterns)
{
    return std::any_of(patterns.begin(), patterns.end(), [&](const SearchPattern& pattern) { return matches_pattern(haystack, pattern); });
}

std::string trim_search_text(std::string_view text)
{
    return trim_text(text);
}

} // namespace slayerlog
