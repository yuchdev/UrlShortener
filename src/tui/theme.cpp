#include "url_shortener/tui/theme.hpp"

namespace url_shortener::tui
{

namespace
{

/// The dark theme (default): cyan accents on a dark background.
const Theme k_dark = {
    ThemeName::dark,
    false,
    /* title        */ {ColorId::cyan, ColorId::none, true, false, false},
    /* tab_active    */ {ColorId::none, ColorId::none, false, false, true},
    /* tab_inactive  */ {ColorId::none, ColorId::none, false, false, false},
    /* selected_row  */ {ColorId::cyan, ColorId::none, true, false, false},
    /* header_row    */ {ColorId::none, ColorId::none, true, false, false},
    /* ok            */ {ColorId::green, ColorId::none, false, false, false},
    /* warning       */ {ColorId::yellow, ColorId::none, false, false, false},
    /* error         */ {ColorId::red, ColorId::none, true, false, false},
    /* hint          */ {ColorId::grey, ColorId::none, false, true, false},
    /* disabled      */ {ColorId::none, ColorId::none, false, true, false},
    /* banner        */ {ColorId::none, ColorId::none, true, false, true},
};

/// The light theme: blue accents tuned for a light background. Differs from
/// @ref k_dark only in the @ref Theme::title, @ref Theme::selected_row and
/// @ref Theme::hint colours (same layout, same glyphs).
const Theme k_light = {
    ThemeName::light,
    false,
    /* title        */ {ColorId::blue, ColorId::none, true, false, false},
    /* tab_active    */ {ColorId::none, ColorId::none, false, false, true},
    /* tab_inactive  */ {ColorId::none, ColorId::none, false, false, false},
    /* selected_row  */ {ColorId::blue, ColorId::none, true, false, false},
    /* header_row    */ {ColorId::none, ColorId::none, true, false, false},
    /* ok            */ {ColorId::green, ColorId::none, false, false, false},
    /* warning       */ {ColorId::yellow, ColorId::none, false, false, false},
    /* error         */ {ColorId::red, ColorId::none, true, false, false},
    /* hint          */ {ColorId::blue, ColorId::none, false, true, false},
    /* disabled      */ {ColorId::none, ColorId::none, false, true, false},
    /* banner        */ {ColorId::none, ColorId::none, true, false, true},
};

/// The mono theme: no colour at all and ASCII-only borders (the legacy-console
/// and tmux fallback). Emphasis is carried by @ref StyleSpec::bold /
/// @ref StyleSpec::inverted and by words/glyphs the screens render, never by
/// colour - so all information survives (accessibility). Every @ref StyleSpec
/// here has @ref ColorId::none for both foreground and background.
const Theme k_mono = {
    ThemeName::mono,
    true,
    /* title        */ {ColorId::none, ColorId::none, true, false, false},
    /* tab_active    */ {ColorId::none, ColorId::none, false, false, true},
    /* tab_inactive  */ {ColorId::none, ColorId::none, false, false, false},
    /* selected_row  */ {ColorId::none, ColorId::none, false, false, true},
    /* header_row    */ {ColorId::none, ColorId::none, true, false, false},
    /* ok            */ {ColorId::none, ColorId::none, true, false, false},
    /* warning       */ {ColorId::none, ColorId::none, true, false, false},
    /* error         */ {ColorId::none, ColorId::none, true, false, true},
    /* hint          */ {ColorId::none, ColorId::none, false, true, false},
    /* disabled      */ {ColorId::none, ColorId::none, false, true, false},
    /* banner        */ {ColorId::none, ColorId::none, true, false, true},
};

}  // namespace

const Theme& theme_for(ThemeName name)
{
    switch (name)
    {
    case ThemeName::light:
        return k_light;
    case ThemeName::mono:
        return k_mono;
    case ThemeName::dark:
    default:
        return k_dark;
    }
}

std::optional<ThemeName> parse_theme_name(const std::string& s)
{
    if (s == "dark")
    {
        return ThemeName::dark;
    }
    if (s == "light")
    {
        return ThemeName::light;
    }
    if (s == "mono")
    {
        return ThemeName::mono;
    }
    return std::nullopt;
}

}  // namespace url_shortener::tui
