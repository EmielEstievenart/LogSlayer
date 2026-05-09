#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <memory>
#include <vector>
#include <utility>

#include "command_manager.hpp"

namespace slayerlog
{

namespace
{

class TestCommand final : public Command
{
public:
    explicit TestCommand(std::string name, bool active_after_execute = false) : _descriptor({std::move(name), "summary", "usage"}), _active_after_execute(active_after_execute) { }

    const CommandDescriptor& descriptor() const override
    {
        return _descriptor;
    }

    CommandResult execute(std::string_view arguments) override
    {
        received_arguments = std::string(arguments);
        active             = _active_after_execute;
        return {true, "ok"};
    }

    bool has_active_interaction() const override
    {
        return active;
    }

    void cancel() override
    {
        cancelled = true;
        active    = false;
    }

    std::optional<HiddenColumnRange> hidden_column_preview(std::string_view arguments) const override
    {
        if (_descriptor.name != "hide-columns")
        {
            return std::nullopt;
        }

        return parse_hidden_column_range(arguments);
    }

    bool active = false;
    bool cancelled = false;
    std::string received_arguments;

private:
    CommandDescriptor _descriptor;
    bool _active_after_execute = false;
};

} // namespace

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

TEST(CommandManagerTest, RegistersObjectBackedCommands)
{
    CommandManager manager;
    manager.register_command(std::make_unique<TestCommand>("alpha"));

    const auto result = manager.execute("alpha value");

    EXPECT_TRUE(result.success);
    ASSERT_EQ(manager.commands().size(), 1U);
    EXPECT_EQ(manager.commands()[0].name, "alpha");
}

TEST(CommandManagerTest, RejectsDuplicateObjectAndAdapterCommandNames)
{
    CommandManager manager;
    manager.register_command(std::make_unique<TestCommand>("alpha"));

    EXPECT_THROW(manager.register_command({"ALPHA", "Duplicate", "alpha"}, [](std::string_view) { return CommandResult {true, "ok"}; }), std::invalid_argument);
}

TEST(CommandManagerTest, TracksAndCancelsActiveCommand)
{
    CommandManager manager;
    auto command = std::make_unique<TestCommand>("interactive", true);
    TestCommand* command_ptr = command.get();
    manager.register_command(std::move(command));

    const auto result = manager.execute("interactive");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(manager.active_command(), command_ptr);

    manager.cancel_active_command();

    EXPECT_EQ(manager.active_command(), nullptr);
    EXPECT_TRUE(command_ptr->cancelled);
    EXPECT_FALSE(command_ptr->active);
}

TEST(CommandManagerTest, HiddenColumnPreviewDispatchesToMatchingCommand)
{
    CommandManager manager;
    manager.register_command(std::make_unique<TestCommand>("hide-columns"));

    const auto preview = manager.hidden_column_preview("hide-columns 7-12");

    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(*preview, (HiddenColumnRange {7, 12}));
}

} // namespace slayerlog
