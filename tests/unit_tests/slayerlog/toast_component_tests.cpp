#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui_components/toast_component.hpp>

namespace
{

std::shared_ptr<ToastHostComponent> make_toast_host()
{
    ToastHostOption option;
    option.width       = 30;
    option.max_visible = std::numeric_limits<int>::max();

    auto content = ftxui::Renderer([] { return ftxui::emptyElement(); });
    return std::make_shared<ToastHostComponent>(content, option);
}

void show_title_toast(const std::shared_ptr<ToastHostComponent>& toast_host, std::string title)
{
    ToastOption option;
    option.title   = std::move(title);
    option.timeout = std::chrono::milliseconds(0);
    (void)toast_host->show(std::move(option));
}

std::string render_to_string(const std::shared_ptr<ToastHostComponent>& toast_host, int width, int height)
{
    auto screen  = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    auto element = toast_host->Render();
    ftxui::Render(screen, element);
    return screen.ToString();
}

} // namespace

TEST(ToastComponentTest, ShowsMoreThanThreeToastsWhenReflectedHeightAllows)
{
    auto toast_host = make_toast_host();
    for (int index = 1; index <= 6; ++index)
    {
        show_title_toast(toast_host, "Toast " + std::to_string(index));
    }

    const std::string output = render_to_string(toast_host, 80, 20);

    EXPECT_NE(output.find("Toast 1"), std::string::npos);
    EXPECT_NE(output.find("Toast 2"), std::string::npos);
    EXPECT_NE(output.find("Toast 3"), std::string::npos);
    EXPECT_NE(output.find("Toast 4"), std::string::npos);
    EXPECT_NE(output.find("Toast 5"), std::string::npos);
    EXPECT_NE(output.find("Toast 6"), std::string::npos);
}

TEST(ToastComponentTest, KeepsNewestToastsThatFitReflectedHeight)
{
    auto toast_host = make_toast_host();
    for (int index = 1; index <= 5; ++index)
    {
        show_title_toast(toast_host, "Toast " + std::to_string(index));
    }

    const std::string output = render_to_string(toast_host, 80, 10);

    EXPECT_EQ(output.find("Toast 1"), std::string::npos);
    EXPECT_EQ(output.find("Toast 2"), std::string::npos);
    EXPECT_NE(output.find("Toast 3"), std::string::npos);
    EXPECT_NE(output.find("Toast 4"), std::string::npos);
    EXPECT_NE(output.find("Toast 5"), std::string::npos);
}
