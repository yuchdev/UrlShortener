#pragma once

#include <optional>
#include <string>

namespace url_shortener::tui
{

/**
 * @brief The colour theme to render with (ADR 0003 "Launching",
 * `--theme dark|light|mono`).
 *
 * The underlying type is fixed to `int` so @ref TuiOptions (which only
 * forward-declares this enum in `tui_options.hpp`) can store and pass a value
 * before this definition is visible. `dark` is value `0`, so a
 * value-initialised @ref ThemeName (`ThemeName{}`) is the dark default.
 */
enum class ThemeName : int
{
    dark,   ///< Colourful theme for dark terminals (the default).
    light,  ///< Colourful theme tuned for light terminals.
    mono,   ///< No colour, ASCII-only borders; the legacy/tmux fallback.
};

/**
 * @brief An FTXUI-free colour identifier (model layer, contract K2).
 *
 * The model library never names an FTXUI `Color`; it uses these semantic
 * identifiers, which the UI library maps to concrete `ftxui::Color` values in
 * `to_decorator` (`theme_ftxui.cpp`). @ref none means "do not set a colour"
 * (the cell keeps the terminal default), which is what every @ref StyleSpec in
 * the @ref ThemeName::mono theme uses.
 */
enum class ColorId
{
    none,    ///< No colour set; inherit the terminal default.
    red,     ///< Error / destructive emphasis.
    green,   ///< Success / "ok" emphasis.
    yellow,  ///< Warning emphasis.
    blue,    ///< Informational / accent.
    cyan,    ///< Accent (dark-theme titles and selection).
    grey,    ///< Dim / secondary text.
};

/**
 * @brief A semantic text style: foreground/background colour and attributes.
 *
 * Screens never name a colour directly; they reference a role on the active
 * @ref Theme (e.g. `theme.error`) and the frame/renderers turn the role's
 * `StyleSpec` into an `ftxui::Decorator`. Attributes (@ref bold, @ref dim,
 * @ref inverted) carry meaning without colour, so they survive the mono theme
 * where @ref fg and @ref bg are always @ref ColorId::none (accessibility: no
 * information is signalled by colour alone).
 */
struct StyleSpec
{
    ColorId fg = ColorId::none;  ///< Foreground colour, or @ref ColorId::none.
    ColorId bg = ColorId::none;  ///< Background colour, or @ref ColorId::none.
    bool bold = false;           ///< Render bold/bright.
    bool dim = false;            ///< Render dimmed/faint.
    bool inverted = false;       ///< Swap foreground and background.
};

/**
 * @brief The full set of semantic styles a theme assigns to UI roles.
 *
 * One immutable table per @ref ThemeName (returned by @ref theme_for). Every
 * role is always populated - the only "empty" style is an all-@ref ColorId::none
 * @ref StyleSpec (e.g. @ref tab_inactive), which is a deliberate "plain text"
 * role, never an uninitialised hole. The three themes keep identical *layout*
 * (same glyphs, same widths); @ref ThemeName::dark and @ref ThemeName::light
 * differ only in colour, while @ref ThemeName::mono additionally sets
 * @ref ascii_only and drops every colour.
 */
struct Theme
{
    ThemeName name;    ///< Which theme this table is (self-identifying).
    bool ascii_only;   ///< `true` only for mono: borders/blocks map to ASCII.

    StyleSpec title;         ///< The frame title bar text.
    StyleSpec tab_active;    ///< The currently selected navigation tab.
    StyleSpec tab_inactive;  ///< A non-selected navigation tab (plain).
    StyleSpec selected_row;  ///< The highlighted row in a table/list.
    StyleSpec header_row;    ///< A table's column-header row.
    StyleSpec ok;            ///< Success text (also carries the word "ok").
    StyleSpec warning;       ///< Warning text (also carries a word/glyph).
    StyleSpec error;         ///< Error text (also carries the word/`error.code`).
    StyleSpec hint;          ///< Footer / help key-hint text.
    StyleSpec disabled;      ///< A disabled (read-only) control.
    StyleSpec banner;        ///< The memory-store / notice banner row.
};

/**
 * @brief Returns the immutable style table for a theme.
 *
 * @param name The theme to look up.
 * @return A reference to a process-lifetime constant @ref Theme; never a
 * dangling or temporary reference, so it is safe to hold for as long as the
 * program runs. An unknown value (outside the enum) falls back to the dark
 * theme so the caller always gets a fully populated table.
 */
const Theme& theme_for(ThemeName name);

/**
 * @brief Parses a `--theme` command-line value into a @ref ThemeName.
 *
 * Accepts exactly the three lower-case launch-line spellings `"dark"`,
 * `"light"` and `"mono"`; the match is case-sensitive, so `"Dark"`, an empty
 * string and any other word (e.g. `"solarized"`) are rejected. Subtask 19 uses
 * this to reject an invalid `--theme` at parse time.
 *
 * @param s The raw flag value.
 * @return The matching @ref ThemeName, or `std::nullopt` when @p s is not one
 * of the three accepted names.
 */
std::optional<ThemeName> parse_theme_name(const std::string& s);

}  // namespace url_shortener::tui
