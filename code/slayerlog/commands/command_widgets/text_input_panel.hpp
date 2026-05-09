#pragma once

#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "command_widgets/editable_text.hpp"

namespace slayerlog
{

class TextInputPanel
{
public:
    bool handle_event(const ftxui::Event& event);
    ftxui::Element render();

    const std::string& text() const;
    void set_preview(std::string preview, bool is_error);

private:
    EditableText _input;
    std::string _preview;
    bool _preview_is_error = false;
};

} // namespace slayerlog
