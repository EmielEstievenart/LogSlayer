#pragma once

#include <functional>

#include <ftxui/dom/node.hpp>

namespace slayerlog
{

class ReflectedViewport : public ftxui::Node
{
public:
    using OnResize = std::function<void(int width, int height)>;

    explicit ReflectedViewport(OnResize on_resize);

    void ComputeRequirement() override;
    void SetBox(ftxui::Box box) override;

private:
    OnResize _on_resize;
};

ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize);

} // namespace slayerlog
