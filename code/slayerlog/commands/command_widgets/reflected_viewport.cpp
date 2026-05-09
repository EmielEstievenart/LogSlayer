#include "command_widgets/reflected_viewport.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace slayerlog
{

ReflectedViewport::ReflectedViewport(OnResize on_resize) : _on_resize(std::move(on_resize)) { }

void ReflectedViewport::ComputeRequirement()
{
    requirement_             = ftxui::Requirement {};
    requirement_.flex_grow_x = 1;
    requirement_.flex_grow_y = 1;
}

void ReflectedViewport::SetBox(ftxui::Box box)
{
    Node::SetBox(box);

    if (_on_resize)
    {
        const int width  = std::max(1, box.x_max - box.x_min + 1);
        const int height = std::max(1, box.y_max - box.y_min + 1);
        _on_resize(width, height);
    }
}

ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize)
{
    return std::make_shared<ReflectedViewport>(std::move(on_resize));
}

} // namespace slayerlog
