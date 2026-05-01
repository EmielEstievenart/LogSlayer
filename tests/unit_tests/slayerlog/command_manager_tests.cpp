#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "command_manager.hpp"

namespace slayerlog
{

TEST(CommandManagerTest, ReturnsAllCommandsForEmptyQuery)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"reset-filters", "Clear all filters", "reset-filters"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    const auto matches = manager.matching_commands("");

    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].name, "filter-in");
    EXPECT_EQ(matches[1].name, "reset-filters");
}

TEST(CommandManagerTest, ReturnsRegisteredCommandsInRegistrationOrder)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>", {"Example: filter-in auth"}}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"reset-filters", "Clear all filters", "reset-filters"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    const auto commands = manager.commands();

    ASSERT_EQ(commands.size(), 2U);
    EXPECT_EQ(commands[0].name, "filter-in");
    EXPECT_EQ(commands[0].summary, "Include matching lines");
    EXPECT_EQ(commands[0].usage, "filter-in <text>");
    ASSERT_EQ(commands[0].help_lines.size(), 1U);
    EXPECT_EQ(commands[0].help_lines[0], "Example: filter-in auth");
    EXPECT_EQ(commands[1].name, "reset-filters");
}

TEST(CommandManagerTest, MatchesTypedCommandNameIgnoringArguments)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"filter-out", "Exclude matching lines", "filter-out <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"reset-filters", "Clear all filters", "reset-filters"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    const auto matches = manager.matching_commands("filter-in error");

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].name, "filter-in");
}

TEST(CommandManagerTest, ExecutesCommandWithRemainingTextAsArguments)
{
    CommandManager manager;
    std::string received_arguments;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"},
                             [&](std::string_view arguments)
                             {
                                 received_arguments = std::string(arguments);
                                 return CommandResult {true, "added"};
                             });

    const auto result = manager.execute("filter-in   some error text   ");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message, "added");
    EXPECT_EQ(received_arguments, "some error text");
}

TEST(CommandManagerTest, ReturnsErrorForUnknownCommand)
{
    CommandManager manager;

    const auto result = manager.execute("missing-command anything");

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Unknown command: missing-command");
}

TEST(CommandManagerTest, RejectsDuplicateCommandNames)
{
    CommandManager manager;
    manager.register_command({"filter-in", "Include matching lines", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; });

    EXPECT_THROW(manager.register_command({"filter-in", "Duplicate", "filter-in <text>"}, [](std::string_view) { return CommandResult {true, "ok"}; }), std::invalid_argument);
}

} // namespace slayerlog
