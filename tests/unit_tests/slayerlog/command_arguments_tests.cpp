#include <gtest/gtest.h>

#include <string>

#include "commands/command_arguments.hpp"

namespace slayerlog
{

TEST(CommandArgumentsTest, SplitsBareArgumentAndRemainder)
{
    const auto split = split_first_command_argument("app.log +00 00:01:00");

    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->value, "app.log");
    EXPECT_EQ(split->remainder, "+00 00:01:00");
}

TEST(CommandArgumentsTest, SplitsQuotedArgumentWithSpaces)
{
    const auto split = split_first_command_argument("\"C:\\my logs\\app.log\" +00 00:01:00");

    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->value, "C:\\my logs\\app.log");
    EXPECT_EQ(split->remainder, "+00 00:01:00");
}

TEST(CommandArgumentsTest, DoubledQuoteInsideQuotedArgumentIsLiteral)
{
    const auto split = split_first_command_argument("\"say \"\"hi\"\"\" rest");

    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->value, "say \"hi\"");
    EXPECT_EQ(split->remainder, "rest");
}

TEST(CommandArgumentsTest, ReportsMissingArgumentAndUnterminatedQuote)
{
    EXPECT_FALSE(split_first_command_argument("").has_value());
    EXPECT_FALSE(split_first_command_argument("   ").has_value());
    EXPECT_FALSE(split_first_command_argument("\"unterminated").has_value());
}

TEST(CommandArgumentsTest, LastArgumentHasEmptyRemainder)
{
    const auto split = split_first_command_argument("only");

    ASSERT_TRUE(split.has_value());
    EXPECT_EQ(split->value, "only");
    EXPECT_TRUE(split->remainder.empty());
}

TEST(CommandArgumentsTest, QuoteAddsQuotesOnlyWhenNeeded)
{
    EXPECT_EQ(quote_command_argument("app.log"), "app.log");
    EXPECT_EQ(quote_command_argument("C:\\my logs\\app.log"), "\"C:\\my logs\\app.log\"");
    EXPECT_EQ(quote_command_argument("say \"hi\""), "\"say \"\"hi\"\"\"");
    EXPECT_EQ(quote_command_argument(""), "\"\"");
}

TEST(CommandArgumentsTest, QuotedArgumentsRoundTripThroughSplit)
{
    for (const std::string original : {"plain", "with space", "tab\there", "quote\"inside", "trailing\\", "C:\\my logs\\app.log"})
    {
        const auto split = split_first_command_argument(quote_command_argument(original) + " tail");
        ASSERT_TRUE(split.has_value()) << original;
        EXPECT_EQ(split->value, original);
        EXPECT_EQ(split->remainder, "tail");
    }
}

} // namespace slayerlog
