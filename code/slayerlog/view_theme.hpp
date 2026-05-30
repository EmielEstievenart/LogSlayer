#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <ftxui/dom/elements.hpp>

namespace slayerlog::theme
{

namespace terminal
{

inline const std::array<ftxui::Color, 16> colors {
    ftxui::Color::Black,
    ftxui::Color::Red,
    ftxui::Color::Green,
    ftxui::Color::Yellow,
    ftxui::Color::Blue,
    ftxui::Color::Magenta,
    ftxui::Color::Cyan,
    ftxui::Color::GrayLight,
    ftxui::Color::GrayDark,
    ftxui::Color::RedLight,
    ftxui::Color::GreenLight,
    ftxui::Color::YellowLight,
    ftxui::Color::BlueLight,
    ftxui::Color::MagentaLight,
    ftxui::Color::CyanLight,
    ftxui::Color::White,
};

inline const auto black         = colors[0];
inline const auto red           = colors[1];
inline const auto green         = colors[2];
inline const auto yellow        = colors[3];
inline const auto blue          = colors[4];
inline const auto magenta       = colors[5];
inline const auto cyan          = colors[6];
inline const auto gray_light    = colors[7];
inline const auto gray_dark     = colors[8];
inline const auto red_light     = colors[9];
inline const auto green_light   = colors[10];
inline const auto yellow_light  = colors[11];
inline const auto blue_light    = colors[12];
inline const auto magenta_light = colors[13];
inline const auto cyan_light    = colors[14];
inline const auto white         = colors[15];

} // namespace terminal

// General UI
inline const auto muted = terminal::gray_dark;

// Focused-panel accent (active view highlight)
inline const auto active_view_fg = terminal::cyan_light;

// Find match highlighting
inline const auto find_match_bg             = terminal::blue;
inline const auto find_match_fg             = terminal::white;
inline const auto find_active_bg            = terminal::yellow;
inline const auto find_active_fg            = terminal::black;
inline const auto hidden_columns_preview_bg = terminal::cyan;
inline const auto selected_line_bg          = terminal::cyan;
inline const auto selected_line_fg          = terminal::black;

// Status messages
inline const auto success_fg = terminal::green_light;
inline const auto error_fg   = terminal::red;

// Status bar labels
inline const auto label_filter_fg = terminal::cyan;
inline const auto label_find_fg   = terminal::yellow;
inline const auto label_align_fg  = terminal::magenta_light;
inline const auto label_key_fg    = terminal::white;
inline const auto paused_fg       = terminal::yellow;

// Toasts
inline const auto toast_info_fg       = terminal::cyan_light;
inline const auto toast_success_fg    = terminal::green_light;
inline const auto toast_warning_fg    = terminal::yellow;
inline const auto toast_error_fg      = terminal::red;
inline const auto toast_background_bg = terminal::gray_dark;

inline ftxui::Color source_tag_color(std::size_t source_index)
{
    static const std::array<ftxui::Color, 8> source_tag_palette {
        terminal::cyan_light,
        terminal::green_light,
        terminal::yellow_light,
        terminal::blue_light,
        terminal::red_light,
        terminal::magenta_light,
        terminal::white,
        terminal::cyan,
    };

    return source_tag_palette[source_index % source_tag_palette.size()];
}

// Scrollbar
inline const auto scrollbar_thumb_fg = terminal::gray_light;
inline const auto scrollbar_track_fg = terminal::gray_dark;

// Helper: apply find-match highlighting to an element
inline ftxui::Element apply_find_highlight(ftxui::Element element, bool is_active_match)
{
    if (is_active_match)
    {
        return std::move(element) | ftxui::bgcolor(find_active_bg) | ftxui::color(find_active_fg);
    }
    return std::move(element) | ftxui::bgcolor(find_match_bg) | ftxui::color(find_match_fg);
}

// Helper: render a colored status badge
inline ftxui::Element badge(const std::string& label, ftxui::Color fg)
{
    return ftxui::text(label) | ftxui::bold | ftxui::color(fg);
}

// Helper: render a keyboard shortcut hint (key in bold, description in muted)
inline ftxui::Element key_hint(const std::string& key, const std::string& description)
{
    return ftxui::hbox({
        ftxui::text(key) | ftxui::bold | ftxui::color(label_key_fg),
        ftxui::text(" " + description) | ftxui::color(muted),
    });
}

} // namespace slayerlog::theme
