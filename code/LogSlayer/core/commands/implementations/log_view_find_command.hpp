#pragma once

#include "core_command.hpp"

namespace slayerlog
{

class LogViewFindManager;

class LogViewFindCommand final : public CoreCommand
{
public:
    explicit LogViewFindCommand(LogViewFindManager& find_manager);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    LogViewFindManager& _find_manager;
};

} // namespace slayerlog
