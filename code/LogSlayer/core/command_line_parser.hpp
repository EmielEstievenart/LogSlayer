#pragma once

#include <string>
#include <vector>

namespace slayerlog
{

class CommandManager;

struct Config
{
    std::vector<std::string> file_paths;
    int poll_interval_ms = 250;
    bool show_help       = false;

    /// Palette command lines from --cmd, executed in order at startup after
    /// the sources and any --config commands.
    std::vector<std::string> startup_commands;

    /// Saved config to replay at startup (--config <name>); empty when unset.
    std::string config_name;

    /// Replay the auto-saved previous session (--resume-last).
    bool resume_last = false;
};

Config parse_command_line(int argc, char* argv[]);
std::string build_help_text(const CommandManager& command_manager);

} // namespace slayerlog
