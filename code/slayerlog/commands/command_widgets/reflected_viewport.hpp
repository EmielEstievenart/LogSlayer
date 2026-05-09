#pragma once

#include <functional>

#include <ftxui/dom/node.hpp>

namespace slayerlog
{

class ReflectedViewport : public ftxui::Node
{
public:
    using OnResize = std::function<void(int width, int height)>;
    using Builder  = std::function<ftxui::Element()>;

    explicit ReflectedViewport(OnResize on_resize);
    ReflectedViewport(OnResize on_resize, Builder builder);

    void ComputeRequirement() override;
    void SetBox(ftxui::Box box) override;

private:
    OnResize _on_resize;
    Builder _builder;
};

ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize);
ftxui::Element reflected_viewport(ReflectedViewport::OnResize on_resize, ReflectedViewport::Builder builder);

} // namespace slayerlog
