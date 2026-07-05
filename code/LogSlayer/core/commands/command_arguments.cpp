#include "commands/command_arguments.hpp"

#include <cctype>

namespace slayerlog
{

namespace
{

std::string_view trim_left(std::string_view text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
    {
        ++start;
    }
    return text.substr(start);
}

bool is_space(char character)
{
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

} // namespace

std::optional<SplitCommandArgument> split_first_command_argument(std::string_view text)
{
    text = trim_left(text);
    if (text.empty())
    {
        return std::nullopt;
    }

    SplitCommandArgument split;

    if (text.front() == '"')
    {
        std::size_t index = 1;
        while (index < text.size())
        {
            if (text[index] == '"')
            {
                // A doubled quote inside a quoted argument is a literal quote.
                if (index + 1 < text.size() && text[index + 1] == '"')
                {
                    split.value.push_back('"');
                    index += 2;
                    continue;
                }

                split.remainder = std::string(trim_left(text.substr(index + 1)));
                return split;
            }

            split.value.push_back(text[index]);
            ++index;
        }

        return std::nullopt;
    }

    std::size_t end = 0;
    while (end < text.size() && !is_space(text[end]))
    {
        ++end;
    }

    split.value     = std::string(text.substr(0, end));
    split.remainder = std::string(trim_left(text.substr(end)));
    return split;
}

std::string quote_command_argument(std::string_view text)
{
    bool needs_quotes = text.empty();
    for (const char character : text)
    {
        if (is_space(character) || character == '"')
        {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes)
    {
        return std::string(text);
    }

    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');
    for (const char character : text)
    {
        if (character == '"')
        {
            quoted.push_back('"');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

} // namespace slayerlog
