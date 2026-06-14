#pragma once

#include <optional>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "core_command.hpp"

namespace slayerlog
{

struct CommandEventResult
{
    bool handled = false;
    std::optional<CommandResult> result;
};

/// UI-facing command: a CoreCommand that can additionally render itself and
/// consume terminal events while it owns an interactive session in the palette.
/// Only commands that draw their own widgets need to derive from this; pure
/// actions derive from CoreCommand directly and live in the core library.
class Command : public CoreCommand
{
public:
    virtual CommandEventResult handle_event(const ftxui::Event&)
    {
        return {};
    }

    virtual ftxui::Element render()
    {
        return ftxui::emptyElement();
    }

    virtual std::string palette_title() const
    {
        return descriptor().name;
    }

    virtual ftxui::Element render_help() const
    {
        return ftxui::emptyElement();
    }
};

} // namespace slayerlog
