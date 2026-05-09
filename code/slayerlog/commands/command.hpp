#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

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

struct CommandEventResult
{
    bool handled = false;
    std::optional<CommandResult> result;
};

class Command
{
public:
    virtual ~Command() = default;

    virtual const CommandDescriptor& descriptor() const = 0;
    virtual CommandResult execute(std::string_view arguments) = 0;

    virtual bool has_active_interaction() const
    {
        return false;
    }

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

    virtual void cancel()
    {
    }

    virtual std::optional<HiddenColumnRange> hidden_column_preview(std::string_view) const
    {
        return std::nullopt;
    }
};

} // namespace slayerlog
