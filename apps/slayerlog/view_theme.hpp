#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <ftxui/dom/elements.hpp>

namespace slayerlog::theme
{

// General UI
inline const auto muted = ftxui::Color::GrayDark;

// Find match highlighting
inline const auto find_match_bg             = ftxui::Color::Blue;
inline const auto find_match_fg             = ftxui::Color::White;
inline const auto find_active_bg            = ftxui::Color::Yellow;
inline const auto find_active_fg            = ftxui::Color::Black;
inline const auto hidden_columns_preview_bg = ftxui::Color::Cyan;

// Status messages
inline const auto success_fg = ftxui::Color::GreenLight;
inline const auto error_fg   = ftxui::Color::Red;

// Status bar labels
inline const auto label_filter_fg = ftxui::Color::Cyan;
inline const auto label_find_fg   = ftxui::Color::Yellow;
inline const auto label_key_fg    = ftxui::Color::White;
inline const auto paused_fg       = ftxui::Color::Yellow;

inline ftxui::Color source_tag_color(std::size_t source_index)
{
    static const std::array<ftxui::Color, 8> source_tag_palette {
        ftxui::Color::CyanLight,
        ftxui::Color::GreenLight,
        ftxui::Color::YellowLight,
        ftxui::Color::BlueLight,
        ftxui::Color::RedLight,
        ftxui::Color::MagentaLight,
        ftxui::Color::White,
        ftxui::Color::Cyan,
    };

    return source_tag_palette[source_index % source_tag_palette.size()];
}

// Scrollbar
inline const auto scrollbar_thumb_fg = ftxui::Color::GrayLight;
inline const auto scrollbar_track_fg = ftxui::Color::GrayDark;

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
