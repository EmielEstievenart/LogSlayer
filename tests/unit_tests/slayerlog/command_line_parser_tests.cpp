#include <boost/program_options/errors.hpp>
#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "command_line_parser.hpp"
#include "command_manager.hpp"
#include "settings_store.hpp"

namespace slayerlog
{

namespace
{

class ArgumentBuffer
{
public:
    ArgumentBuffer(std::initializer_list<const char*> arguments)
    {
        _storage.reserve(arguments.size());
        for (const char* item : arguments)
        {
            _storage.emplace_back(item);
        }

        _argv.reserve(_storage.size());
        for (std::string& argument : _storage)
        {
            _argv.push_back(argument.data());
        }
    }

    [[nodiscard]] int argc() const noexcept { return static_cast<int>(_argv.size()); }

    [[nodiscard]] char** argv() noexcept { return _argv.data(); }

private:
    std::vector<std::string> _storage;
    std::vector<char*> _argv;
};

} // namespace

TEST(CommandLineParserTest, AllowsStartingWithoutAnyFiles)
{
    ArgumentBuffer arguments {"slayerlog"};

    const auto config = parse_command_line(arguments.argc(), arguments.argv());

    EXPECT_TRUE(config.file_paths.empty());
    EXPECT_EQ(config.poll_interval_ms, 250);
    EXPECT_FALSE(config.show_help);
}

TEST(CommandLineParserTest, ParsesProvidedFilesAndPollInterval)
{
    ArgumentBuffer arguments {"slayerlog", "--file", "a.log", "--file", "b.log", "--poll-interval-ms", "125"};

    const auto config = parse_command_line(arguments.argc(), arguments.argv());

    ASSERT_EQ(config.file_paths.size(), 2U);
    EXPECT_EQ(config.file_paths[0], "a.log");
    EXPECT_EQ(config.file_paths[1], "b.log");
    EXPECT_EQ(config.poll_interval_ms, 125);
    EXPECT_FALSE(config.show_help);
}

TEST(CommandLineParserTest, ParsesPositionalFiles)
{
    ArgumentBuffer arguments {"slayerlog", "first.log", "second.log"};

    const auto config = parse_command_line(arguments.argc(), arguments.argv());

    ASSERT_EQ(config.file_paths.size(), 2U);
    EXPECT_EQ(config.file_paths[0], "first.log");
    EXPECT_EQ(config.file_paths[1], "second.log");
    EXPECT_FALSE(config.show_help);
}

TEST(CommandLineParserTest, ReturnsShowHelpFlagWithoutExiting)
{
    ArgumentBuffer arguments {"slayerlog", "--help"};

    const auto config = parse_command_line(arguments.argc(), arguments.argv());

    EXPECT_TRUE(config.show_help);
    EXPECT_TRUE(config.file_paths.empty());
    EXPECT_EQ(config.poll_interval_ms, 250);
}

TEST(CommandLineParserTest, BuildHelpTextIncludesRegisteredCommands)
{
    CommandManager manager;
    manager.register_command({"filter-in",
                              "Show lines matching text or regex",
                              "filter-in <text|re:regex>",
                              {
                                  "Use plain text for substring matching or prefix with re: for a regular expression.",
                                  "Example: filter-in re:^(ERROR|WARN)",
                              }},
                             [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"open-file", "Open file and reload all tracked logs", "open-file <path>"}, [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"close-open-file",
                              "Close one currently open file",
                              "close-open-file",
                              {
                                  "Opens a picker containing the currently tracked sources.",
                                  "Use Up/Down to select a source and Enter to close it.",
                              }},
                             [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"find",
                              "Find lines matching text or regex",
                              "find <text|re:regex>",
                              {
                                  "After find is active, use Right/Left to move between matches and Esc to clear it.",
                              }},
                             [](std::string_view) { return CommandResult {true, "ok"}; });
    manager.register_command({"open-folder",
                              "Open folder and reload all tracked logs",
                              "open-folder <path>",
                              {
                                  "Folder watching also opens .zst files and reads them through the zstd watcher.",
                              }},
                             [](std::string_view) { return CommandResult {true, "ok"}; });

    const std::string help_text = build_help_text(manager);

    EXPECT_NE(help_text.find("Slayerlog Options"), std::string::npos);
    EXPECT_NE(help_text.find("Command Palette Commands:"), std::string::npos);
    EXPECT_NE(help_text.find("Source Syntax:"), std::string::npos);
    EXPECT_NE(help_text.find("Config Storage:"), std::string::npos);
    EXPECT_NE(help_text.find("Viewer Keys:"), std::string::npos);
    EXPECT_NE(help_text.find("Command Palette Controls:"), std::string::npos);
    EXPECT_NE(help_text.find("Ctrl+F opens the command palette with find prefilled."), std::string::npos);
    EXPECT_NE(help_text.find("ssh://user@example.com/var/log/app.log"), std::string::npos);
    EXPECT_NE(help_text.find("app.log worker.log"), std::string::npos);
    EXPECT_NE(help_text.find("Source to open on startup."), std::string::npos);
    EXPECT_NE(help_text.find(default_settings_file_path().string()), std::string::npos);
    EXPECT_NE(help_text.find("Settings, command history, and timestamp formats are stored in:"), std::string::npos);
    EXPECT_NE(help_text.find("Windows default: %LOCALAPPDATA%/slayerlog/settings.ini"), std::string::npos);
    EXPECT_NE(help_text.find("macOS default: ~/Library/Application Support/slayerlog/settings.ini."), std::string::npos);
    EXPECT_NE(help_text.find("Linux default: $XDG_CONFIG_HOME/slayerlog/settings.ini"), std::string::npos);
    EXPECT_NE(help_text.find("filter-in <text|re:regex>"), std::string::npos);
    EXPECT_NE(help_text.find("Show lines matching text or regex"), std::string::npos);
    EXPECT_NE(help_text.find("Example: filter-in re:^(ERROR|WARN)"), std::string::npos);
    EXPECT_NE(help_text.find("open-file <path>"), std::string::npos);
    EXPECT_NE(help_text.find("Use Up/Down to select a source and Enter to close it."), std::string::npos);
    EXPECT_NE(help_text.find("After find is active, use Right/Left to move between matches and Esc to clear it."), std::string::npos);
    EXPECT_NE(help_text.find("Folder watching also opens .zst files and reads them through the zstd watcher."), std::string::npos);
}

TEST(CommandLineParserTest, ThrowsOnNonPositivePollInterval)
{
    ArgumentBuffer arguments {"slayerlog", "--poll-interval-ms", "0"};

    EXPECT_THROW(parse_command_line(arguments.argc(), arguments.argv()), boost::program_options::error);
}

} // namespace slayerlog
