#pragma once

#include "core_command.hpp"

namespace slayerlog
{

class LogView2FindManager;

class LogView2FindCommand final : public CoreCommand
{
public:
    explicit LogView2FindCommand(LogView2FindManager& find_manager);

    const CommandDescriptor& descriptor() const override;
    CommandResult execute(std::string_view arguments) override;

private:
    LogView2FindManager& _find_manager;
};

} // namespace slayerlog
