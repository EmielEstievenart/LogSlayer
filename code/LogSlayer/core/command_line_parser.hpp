#pragma once

#include <string>
#include <vector>

namespace slayerlog
{

class CommandManager;

/// Which user interface the process should run.
enum class UiKind
{
    Tui,
    Gui,
};

struct Config
{
    std::vector<std::string> file_paths;
    int poll_interval_ms = 250;
    bool show_help       = false;
    UiKind ui            = UiKind::Tui;
};

Config parse_command_line(int argc, char* argv[]);
std::string build_help_text(const CommandManager& command_manager);

} // namespace slayerlog
