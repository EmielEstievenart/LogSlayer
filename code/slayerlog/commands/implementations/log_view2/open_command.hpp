#pragma once

#include "command.hpp"
#include "command_context.hpp"
#include "implementations/log_view1/open_file_command.hpp"
#include "implementations/log_view1/open_folder_command.hpp"

namespace slayerlog
{

class OpenCommand final : public Command
{
public:
    explicit OpenCommand(CommandContext context);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    OpenFileCommand _open_file_command;
    OpenFolderCommand _open_folder_command;
};

} // namespace slayerlog
