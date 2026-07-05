#pragma once

#include <functional>
#include <string>

#include "command_context.hpp"
#include "commands/core_command.hpp"

namespace slayerlog
{

class ExportConfigCommand final : public CoreCommand
{
public:
    using ClipboardWriter    = std::function<bool(const std::string& text, std::string& error_message)>;
    using ExecutableResolver = std::function<std::string()>;

    explicit ExportConfigCommand(CommandContext context);
    ExportConfigCommand(CommandContext context, ClipboardWriter clipboard_writer, ExecutableResolver executable_resolver);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    CommandContext _context;
    ClipboardWriter _clipboard_writer;
    ExecutableResolver _executable_resolver;
};

} // namespace slayerlog
