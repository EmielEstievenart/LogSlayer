#pragma once

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace slayerlog
{

struct SearchPattern
{
    std::string raw_text;
    std::string needle;
    std::optional<std::regex> regex;
};

SearchPattern compile_search_pattern(std::string_view text);
bool matches_pattern(std::string_view haystack, const SearchPattern& pattern);
bool matches_any_pattern(std::string_view haystack, const std::vector<SearchPattern>& patterns);
std::string trim_search_text(std::string_view text);

} // namespace slayerlog
