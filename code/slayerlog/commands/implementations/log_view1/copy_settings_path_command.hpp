#pragma once

#include <functional>
#include <string>

#include "command.hpp"
#include "command_context.hpp"

namespace slayerlog
{

class CopySettingsPathCommand final : public Command
{
public:
    using ClipboardWriter = std::function<bool(const std::string& text, std::string& error_message)>;

    explicit CopySettingsPathCommand(CommandContext context);
    CopySettingsPathCommand(CommandContext context, ClipboardWriter clipboard_writer);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    ClipboardWriter _clipboard_writer;
};

} // namespace slayerlog
