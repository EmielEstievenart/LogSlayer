#include "command_widgets/reflected_viewport.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace slayerlog
{

namespace
{

void layout_element(ftxui::Element& element, ftxui::Box box)
{
    ftxui::Node::Status status;
    element->Check(&status);
    constexpr int max_iterations = 20;
    while (status.need_iteration && status.iteration < max_iterations)
    {
        element->ComputeRequirement();
        element->SetBox(box);
        status.need_iteration = false;
        ++status.iteration;
        element->Check(&status);
    }
}

} // namespace

ReflectedViewport::ReflectedViewport(OnResize on_resize) : _on_resize(std::move(on_resize)) { }

ReflectedViewport::ReflectedViewport(OnResize on_resize, Builder builder) : _on_resize(std::move(on_resize)), _builder(std::move(builder)) { }

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

    if (_builder)
    {
        children_.clear();
        children_.push_back(_builder());
        layout_element(children_[0], box);
    }
}

ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize)
{
    return std::make_shared<ReflectedViewport>(std::move(on_resize));
}

ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize, ReflectedViewport::Builder builder)
{
    return std::make_shared<ReflectedViewport>(std::move(on_resize), std::move(builder));
}

} // namespace slayerlog
