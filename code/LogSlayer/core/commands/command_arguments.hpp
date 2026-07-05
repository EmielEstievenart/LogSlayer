#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace slayerlog
{

/// One extracted command argument plus whatever text follows it.
struct SplitCommandArgument
{
    std::string value;
    std::string remainder;
};

/// Extracts the first argument from @p text. A leading double quote starts a
/// quoted argument in which "" denotes a literal quote; otherwise the argument
/// runs to the first whitespace. The remainder is returned left-trimmed.
/// Returns std::nullopt when @p text holds no argument or a quoted argument is
/// left unterminated.
std::optional<SplitCommandArgument> split_first_command_argument(std::string_view text);

/// Renders @p text as a single command argument: wraps it in double quotes
/// (doubling embedded quotes) when it is empty or contains whitespace or
/// quotes, so split_first_command_argument reads it back verbatim.
std::string quote_command_argument(std::string_view text);

} // namespace slayerlog
