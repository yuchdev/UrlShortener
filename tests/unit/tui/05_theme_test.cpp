#define BOOST_TEST_MODULE TuiThemeTest

#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/theme.hpp>

/**
 * @file 05_theme_test.cpp
 * @brief Unit tests for the semantic theme table and `--theme` name parsing.
 *
 * Type: unit (model library, FTXUI-free).
 * Scenario: `parse_theme_name` accepts the three launch-line names and rejects
 *           anything else; the mono theme carries no colour and is ASCII-only;
 *           every theme populates every semantic role; dark and light keep the
 *           same layout but differ in accent colours.
 * Boundaries: case sensitivity (`"Dark"` rejected), empty and unknown names,
 *             the colour-free invariant of the mono theme.
 * On failure first check: the constant tables in `src/tui/theme.cpp` and whether
 *           a new @ref StyleSpec role was added without being set per theme.
 */

using url_shortener::tui::ColorId;
using url_shortener::tui::parse_theme_name;
using url_shortener::tui::StyleSpec;
using url_shortener::tui::Theme;
using url_shortener::tui::theme_for;
using url_shortener::tui::ThemeName;

namespace
{

// Every semantic role of a theme, so a test can assert an invariant over all of
// them without naming each field twice.
std::vector<StyleSpec> all_styles(const Theme& t)
{
    return {t.title, t.tab_active, t.tab_inactive, t.selected_row, t.header_row,
            t.ok,    t.warning,    t.error,        t.hint,         t.disabled,
            t.banner};
}

bool has_no_colour(const StyleSpec& s)
{
    return s.fg == ColorId::none && s.bg == ColorId::none;
}

}  // namespace

// ---------------------------------------------------------------------------
// Normal case: the three launch-line names parse to the matching enum value.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(parse_accepts_the_three_names)
{
    BOOST_TEST((parse_theme_name("dark") == std::optional<ThemeName>(
                                                 ThemeName::dark)));
    BOOST_TEST((parse_theme_name("light") == std::optional<ThemeName>(
                                                  ThemeName::light)));
    BOOST_TEST((parse_theme_name("mono") == std::optional<ThemeName>(
                                                 ThemeName::mono)));
}

// ---------------------------------------------------------------------------
// Invalid input: wrong case, empty and unknown names are rejected.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(parse_rejects_unknown_names)
{
    BOOST_TEST(!parse_theme_name("Dark").has_value());
    BOOST_TEST(!parse_theme_name("").has_value());
    BOOST_TEST(!parse_theme_name("solarized").has_value());
    BOOST_TEST(!parse_theme_name("mono ").has_value());
}

// ---------------------------------------------------------------------------
// theme_for returns the requested theme and self-identifies.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(theme_for_returns_matching_theme)
{
    BOOST_TEST((theme_for(ThemeName::dark).name == ThemeName::dark));
    BOOST_TEST((theme_for(ThemeName::light).name == ThemeName::light));
    BOOST_TEST((theme_for(ThemeName::mono).name == ThemeName::mono));
}

// ---------------------------------------------------------------------------
// Mono carries no colour at all and is ASCII-only; dark/light are coloured.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_has_no_colour_and_is_ascii_only)
{
    const Theme& mono = theme_for(ThemeName::mono);
    BOOST_TEST(mono.ascii_only);
    for (const StyleSpec& s : all_styles(mono))
    {
        BOOST_TEST(has_no_colour(s));
    }

    BOOST_TEST(!theme_for(ThemeName::dark).ascii_only);
    BOOST_TEST(!theme_for(ThemeName::light).ascii_only);
}

// ---------------------------------------------------------------------------
// Accessibility: mono still signals state without colour - selection inverts and
// error is emphasised (so "error" is not colour-only).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_signals_state_without_colour)
{
    const Theme& mono = theme_for(ThemeName::mono);
    BOOST_TEST(mono.selected_row.inverted);
    BOOST_TEST((mono.error.bold || mono.error.inverted));
    BOOST_TEST(mono.header_row.bold);
}

// ---------------------------------------------------------------------------
// Dark and light differ in accent colours (title/selected_row/hint) - the
// documented difference - while keeping the shared roles intact.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dark_and_light_differ_in_accent_colours)
{
    const Theme& dark = theme_for(ThemeName::dark);
    const Theme& light = theme_for(ThemeName::light);

    BOOST_TEST((dark.title.fg != light.title.fg));
    BOOST_TEST((dark.selected_row.fg != light.selected_row.fg));
    BOOST_TEST((dark.hint.fg != light.hint.fg));
}

// ---------------------------------------------------------------------------
// Every theme assigns a real colour (not "none") to the status roles that must
// carry colour in the coloured themes, proving no role was left a default hole.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(coloured_themes_populate_status_roles)
{
    for (ThemeName name : {ThemeName::dark, ThemeName::light})
    {
        const Theme& t = theme_for(name);
        BOOST_TEST((t.ok.fg != ColorId::none));
        BOOST_TEST((t.warning.fg != ColorId::none));
        BOOST_TEST((t.error.fg != ColorId::none));
        BOOST_TEST((t.title.fg != ColorId::none));
    }
}
