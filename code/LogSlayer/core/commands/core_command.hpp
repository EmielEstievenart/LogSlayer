#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "log_types.hpp"

namespace slayerlog
{

struct CommandDescriptor
{
    std::string name;
    std::string summary;
    std::string usage;
    std::vector<std::string> help_lines;
};

struct CommandResult
{
    bool success = false;
    std::string message;
    bool close_palette_on_success = true;

    CommandResult() = default;

    CommandResult(bool success_value, std::string message_value, bool close_palette_on_success_value = true) : success(success_value), message(std::move(message_value)), close_palette_on_success(close_palette_on_success_value) { }
};

/// UI-agnostic command contract. A CoreCommand carries its descriptor and runs
/// its action against the core model; it must not depend on any UI framework.
/// Interactive, render-capable commands extend this through the UI `Command`
/// class (see command.hpp). CommandManager only ever talks to this interface,
/// which is what lets the command system live in the core library.
class CoreCommand
{
public:
    virtual ~CoreCommand() = default;

    virtual const CommandDescriptor& descriptor() const = 0;
    virtual CommandResult execute(std::string_view arguments) = 0;

    /// True while the command owns an interactive session (its own event loop in
    /// the palette). Pure actions return false and complete inside execute().
    virtual bool has_active_interaction() const
    {
        return false;
    }

    virtual void cancel()
    {
    }

    virtual std::optional<HiddenColumnRange> hidden_column_preview(std::string_view) const
    {
        return std::nullopt;
    }
};

} // namespace slayerlog
