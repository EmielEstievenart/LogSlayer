#include "command_line_parser.hpp"

#include "commands/command_manager.hpp"
#include "settings_store.hpp"

#include <boost/program_options.hpp>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

namespace slayerlog
{

namespace
{

void add_command_line_options(boost::program_options::options_description& desc)
{
    namespace po = boost::program_options;

    // clang-format off
    desc.add_options()
        ("help,h", "Show help message")
        ("file,f", po::value<std::vector<std::string>>()->composing(), "Source to open on startup. Repeat for multiple sources; also supports positional sources and ssh://user@host/absolute/path.log.")
        ("poll-interval-ms", po::value<int>()->default_value(250), "Polling interval in milliseconds");
    // clang-format on
}

void append_help_section(std::ostringstream& output, std::string_view title, const std::vector<std::string>& lines)
{
    if (lines.empty())
    {
        return;
    }

    output << '\n' << title << ":\n";
    for (const auto& line : lines)
    {
        output << "  " << line << '\n';
    }
}

std::vector<std::string> build_config_storage_lines()
{
    std::vector<std::string> lines;
    lines.push_back("Settings, command history, and timestamp formats are stored in: " + default_settings_file_path().string());

    lines.push_back("Windows default: %LOCALAPPDATA%/slayerlog/settings.ini, falling back to %APPDATA%/slayerlog/settings.ini.");
    lines.push_back("macOS default: ~/Library/Application Support/slayerlog/settings.ini.");
    lines.push_back("Linux default: $XDG_CONFIG_HOME/slayerlog/settings.ini, falling back to ~/.config/slayerlog/settings.ini.");
    lines.push_back("If those base directories are unavailable, slayerlog falls back to ./slayerlog_settings.ini in the current working directory.");
    return lines;
}

} // namespace

Config parse_command_line(int argc, char* argv[])
{
    namespace po = boost::program_options;

    po::options_description desc("Slayerlog Options");
    add_command_line_options(desc);

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }

    po::positional_options_description positional;
    positional.add("file", -1);

    po::variables_map variables;

    try
    {
        auto parser = po::command_line_parser(arguments).options(desc).positional(positional);

        po::store(parser.run(), variables);
        po::notify(variables);

        Config config;

        if (variables.count("help") != 0U)
        {
            config.show_help = true;
            return config;
        }

        if (variables.count("file") != 0U)
        {
            config.file_paths = variables["file"].as<std::vector<std::string>>();
        }
        config.poll_interval_ms = variables["poll-interval-ms"].as<int>();

        if (config.poll_interval_ms <= 0)
        {
            throw po::error("--poll-interval-ms must be greater than 0");
        }

        return config;
    }
    catch (const po::error& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        std::cerr << desc << '\n';
        throw;
    }
}

std::string build_help_text(const CommandManager& command_manager)
{
    boost::program_options::options_description desc("Slayerlog Options");
    add_command_line_options(desc);

    std::ostringstream output;
    output << desc << '\n';

    append_help_section(output, "Startup Examples",
                        {
                            "slayerlog app.log",
                            "slayerlog app.log worker.log",
                            "slayerlog --file app.log --file worker.log",
                            "slayerlog --file ssh://user@example.com/var/log/app.log",
                            "Press Ctrl+P in the UI to open the command palette for more sources and filters.",
                        });

    append_help_section(output, "Source Syntax",
                        {
                            "Local files: pass a normal path such as logs/app.log or C:/logs/app.log.",
                            "Startup sources can be passed positionally or repeated with --file.",
                            "Remote SSH files: use ssh://user@host/absolute/path.log.",
                            "Remote SSH paths must be absolute, for example ssh://user@example.com/var/log/app.log.",
                            "Local folders are opened from the command palette with open-folder <path>.",
                        });

    append_help_section(output, "Config Storage", build_config_storage_lines());

    append_help_section(output, "Viewer Keys",
                        {
                            "Ctrl+P opens the command palette.",
                            "Ctrl+F opens the command palette with find prefilled.",
                            "Ctrl+R opens command history from the main view or toggles history inside the palette.",
                            "Right jumps to the next visible find match; Left jumps to the previous one.",
                            "Esc clears the active find state.",
                            "q quits the viewer.",
                        });

    append_help_section(output, "Command Palette Controls",
                        {
                            "Type to filter commands by name.",
                            "Tab autocompletes the selected command.",
                            "Enter runs the selected command.",
                            "Ctrl+R switches between commands and command history.",
                            "Esc closes the palette.",
                        });

    const auto commands = command_manager.commands();
    if (commands.empty())
    {
        return output.str();
    }

    output << "\nCommand Palette Commands:\n";

    std::size_t usage_width = 0;
    for (const auto& command : commands)
    {
        usage_width = std::max(usage_width, command.usage.size());
    }

    for (const auto& command : commands)
    {
        output << "  " << command.usage;
        if (command.usage.size() < usage_width)
        {
            output << std::string(usage_width - command.usage.size(), ' ');
        }

        if (!command.summary.empty())
        {
            output << "  " << command.summary;
        }

        output << '\n';

        for (const auto& help_line : command.help_lines)
        {
            output << "      " << help_line << '\n';
        }
    }

    return output.str();
}

} // namespace slayerlog
