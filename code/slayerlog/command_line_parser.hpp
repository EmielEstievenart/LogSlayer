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
};

Config parse_command_line(int argc, char* argv[]);
std::string build_help_text(const CommandManager& command_manager);

} // namespace slayerlog
