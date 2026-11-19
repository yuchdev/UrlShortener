#define BOOST_TEST_MODULE TuiFormWidgetsRenderTest

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>

#include "golden.hpp"
#include "hostile_strings.hpp"
#include "inline_poster.hpp"
#include "mock_command_client.hpp"
#include "render_helpers.hpp"

/**
 * @file 08_form_widgets_render_test.cpp
 * @brief Render + golden tests for the D5 form widgets and body (06.0/08).
 *
 * Type: unit (render/snapshot, links url_shortener_tui + FTXUI).
 * Scenario: each ArgKind widget renders its expected shape; the empty
 *           link.create form, its required-field error state, a > 3-value enum
 *           dropdown and a scrolling tall form match their goldens; the mono
 *           theme is pure ASCII; T-110 - every hostile prefill value renders
 *           with no display hazard in any widget or in the whole form.
 * Boundaries: <= 3-value radio vs > 3-value dropdown; empty vs error; dark vs
 *           mono; 80x24 vs 80x12 (scroll).
 * On failure first check: whether a widget stopped calling
 *           sanitize_for_terminal, or the D5 layout in form_view.cpp changed
 *           (regenerate goldens with UPDATE_GOLDEN=1 and review).
 */

namespace tui = url_shortener::tui;

using tui::ArgInfo;
using tui::ArgKind;
using tui::CommandInfo;
using tui::FormModel;
using tui::FormViewContext;
using tui::make_field_widget;
using tui::make_form_component;
using tui::Theme;
using tui::theme_for;
using tui::ThemeName;
using tui::testing::assert_matches_golden;
using tui::testing::has_display_hazard;
using tui::testing::hostile_samples;
using tui::testing::render_to_text;
using tui::testing::split_lines;

namespace
{

ArgInfo arg(const std::string& name,
            ArgKind kind,
            bool required = false,
            bool nullable = false)
{
    ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    info.nullable = nullable;
    return info;
}

CommandInfo link_create_info()
{
    CommandInfo info;
    info.name = "link.create";
    info.permission = "write";
    info.mutating = true;
    info.supports_dry_run = true;
    ArgInfo target = arg("target_url", ArgKind::string, true);
    ArgInfo slug = arg("slug", ArgKind::string);
    slug.doc = "blank = generated";
    ArgInfo redirect = arg("redirect_type", ArgKind::enumeration);
    redirect.enum_values = {"permanent", "temporary"};
    ArgInfo campaign = arg("campaign", ArgKind::object, false, true);
    campaign.members = {"name", "source", "medium"};
    info.args = {target,
                 slug,
                 redirect,
                 arg("expires_at", ArgKind::timestamp, false, true),
                 arg("enabled", ArgKind::boolean),
                 arg("tags", ArgKind::string_list),
                 arg("metadata", ArgKind::string_map),
                 campaign};
    return info;
}

// A synthetic command exercising every ArgKind (success criterion 2: any
// registry command renders through FormScreen with no TUI code change).
CommandInfo every_kind_info()
{
    CommandInfo info;
    info.name = "demo.every";
    info.permission = "write";
    info.mutating = true;
    ArgInfo en = arg("choice", ArgKind::enumeration);
    en.enum_values = {"a", "b", "c"};
    ArgInfo big = arg("sort", ArgKind::enumeration);
    big.enum_values = {"created_at", "slug", "updated_at", "clicks", "status"};
    ArgInfo obj = arg("campaign", ArgKind::object);
    obj.members = {"name", "source"};
    info.args = {arg("s", ArgKind::string),
                 arg("n", ArgKind::integer),
                 arg("ts", ArgKind::timestamp),
                 arg("b", ArgKind::boolean),
                 en,
                 big,
                 arg("list", ArgKind::string_list),
                 arg("map", ArgKind::string_map),
                 obj};
    return info;
}

// A tall command (more rows than an 80x12 screen) for the scroll golden.
CommandInfo tall_info()
{
    CommandInfo info;
    info.name = "demo.tall";
    info.permission = "write";
    info.mutating = true;
    for (int i = 0; i < 12; ++i) {
        info.args.push_back(arg("field_" + std::to_string(i), ArgKind::string));
    }
    return info;
}

/// Owns the collaborators a FormViewContext references, so a test can build the
/// form body with one line.
struct FormHarness
{
    tui::testing::MockCommandClient client;
    tui::testing::InlinePoster poster;
    ManualClock clock {std::chrono::system_clock::time_point {}};
    tui::ConfirmPolicy policy {false};
    tui::ToastQueue toasts {clock};
    tui::AsyncCaller caller {client, poster, 1};
    const Theme& theme;

    explicit FormHarness(ThemeName t)
        : theme(theme_for(t))
    {
    }

    FormViewContext context(const std::string& title)
    {
        return FormViewContext {caller,
                                policy,
                                toasts,
                                theme,
                                tui::CapabilityModel(tui::SessionInfo {}),
                                title,
                                nullptr,
                                nullptr,
                                nullptr};
    }
};

// The measured-width approach needs one frame to learn the box; the goldens use
// the second (stable) frame.
std::string render_twice(ftxui::Component view, int width, int height)
{
    render_to_text(view, width, height);
    return render_to_text(view, width, height);
}

std::size_t index_of(const FormModel& model, const std::string& name)
{
    const std::vector<tui::FormField>& fields = model.fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].spec.name == name) {
            return i;
        }
    }
    BOOST_FAIL("no field named " << name);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Each ArgKind widget renders its expected shape with no display hazard.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(each_argkind_widget_renders)
{
    const Theme& theme = theme_for(ThemeName::dark);
    FormModel model(every_kind_info());
    model.set_text(index_of(model, "s"), "hello");
    model.set_checked(index_of(model, "b"), true);
    model.set_choice(index_of(model, "choice"), 1);  // b
    model.set_choice(index_of(model, "sort"), 2);  // updated_at

    for (std::size_t i = 0; i < model.fields().size(); ++i) {
        const std::string grid =
            render_to_text(make_field_widget(model, i, theme), 60, 1);
        BOOST_TEST(
            !has_display_hazard(grid),
            "hazard in widget for field " << model.fields()[i].spec.name);
    }

    // Spot-check the distinctive shapes.
    BOOST_TEST(render_to_text(
                   make_field_widget(model, index_of(model, "b"), theme), 10, 1)
                   .find("[x]")
               != std::string::npos);
    BOOST_TEST(
        render_to_text(
            make_field_widget(model, index_of(model, "choice"), theme), 30, 1)
            .find("(*) b")
        != std::string::npos);
}

// ---------------------------------------------------------------------------
// The empty D5 new-link form matches its golden at 80x24.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(new_link_form_matches_golden)
{
    FormHarness h(ThemeName::dark);
    FormModel model(link_create_info());
    FormViewContext ctx = h.context("New link");
    const std::string actual =
        render_twice(make_form_component(model, ctx), 80, 24);

    BOOST_TEST(actual.find("command: link.create") != std::string::npos);
    BOOST_TEST(actual.find("target_url*") != std::string::npos);
    BOOST_TEST(actual.find("[ Create ]") != std::string::npos);
    BOOST_TEST(actual.find("[ Cancel ]") != std::string::npos);
    BOOST_TEST(actual.find("Equivalent:") != std::string::npos);
    assert_matches_golden(actual, "d05_new_link_80x24.txt");
}

// ---------------------------------------------------------------------------
// A required field left empty shows a validation error after a submit attempt.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(required_field_error_matches_golden)
{
    FormHarness h(ThemeName::dark);
    FormModel model(link_create_info());
    // Touch the required field then clear it so validate() flags it.
    model.set_text(index_of(model, "target_url"), "x");
    model.set_text(index_of(model, "target_url"), "");
    model.validate();

    FormViewContext ctx = h.context("New link");
    const std::string actual =
        render_twice(make_form_component(model, ctx), 80, 24);
    assert_matches_golden(actual, "d05_new_link_errors_80x24.txt");
}

// ---------------------------------------------------------------------------
// A > 3-value enumeration renders as a dropdown, not a radio row.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(enum_dropdown_matches_golden)
{
    FormHarness h(ThemeName::dark);
    FormModel model(every_kind_info());
    model.set_choice(index_of(model, "sort"), 0);  // created_at
    FormViewContext ctx = h.context("Run demo.every");
    const std::string actual =
        render_twice(make_form_component(model, ctx), 80, 24);

    BOOST_TEST(actual.find("[created_at") != std::string::npos);
    BOOST_TEST(actual.find("v]") != std::string::npos);
    assert_matches_golden(actual, "d05_generic_enum_dropdown_80x24.txt");
}

// ---------------------------------------------------------------------------
// A form taller than the screen scrolls and keeps the focused row visible.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(tall_form_scrolls_matches_golden)
{
    FormHarness h(ThemeName::dark);
    FormModel model(tall_info());
    FormViewContext ctx = h.context("Tall");
    ftxui::Component form = make_form_component(model, ctx);

    // Tab down several rows so a later field holds focus; the yframe must keep
    // it visible.
    render_to_text(form, 80, 12);
    for (int i = 0; i < 9; ++i) {
        form->OnEvent(ftxui::Event::Tab);
    }
    const std::string actual = render_twice(form, 80, 12);
    assert_matches_golden(actual, "d05_form_scroll_80x12.txt");
}

// ---------------------------------------------------------------------------
// The mono render is pure ASCII (box glyphs folded).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mono_form_is_ascii)
{
    FormHarness h(ThemeName::mono);
    FormModel model(link_create_info());
    FormViewContext ctx = h.context("New link");
    const std::string actual =
        render_twice(make_form_component(model, ctx), 80, 24);
    for (unsigned char byte : actual) {
        BOOST_TEST(byte <= 0x7Eu,
                   "non-ASCII byte in mono grid: " << static_cast<int>(byte));
    }
}

// ---------------------------------------------------------------------------
// T-110: a hostile prefill value never reaches the grid, as a field widget and
// as a whole-form render, in either theme.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(hostile_prefill_never_reaches_grid)
{
    for (const auto& sample : hostile_samples()) {
        for (ThemeName theme_name : {ThemeName::dark, ThemeName::mono}) {
            FormHarness h(theme_name);
            FormModel model(link_create_info());
            model.set_text(index_of(model, "target_url"), sample.raw);
            model.set_text(index_of(model, "tags"), sample.raw);

            // Each individual widget.
            for (std::size_t i = 0; i < model.fields().size(); ++i) {
                const std::string g =
                    render_to_text(make_field_widget(model, i, h.theme), 70, 1);
                for (const std::string& line : split_lines(g)) {
                    BOOST_TEST(!has_display_hazard(line),
                               "widget hazard: " << sample.name);
                }
            }

            // The whole form body.
            FormViewContext ctx = h.context(sample.raw);  // hostile title too
            const std::string grid =
                render_twice(make_form_component(model, ctx), 80, 24);
            BOOST_TEST(grid.find('\x1b') == std::string::npos,
                       "ESC in form grid: " << sample.name);
            for (const std::string& line : split_lines(grid)) {
                BOOST_TEST(!has_display_hazard(line),
                           "form hazard: " << sample.name);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// T-121: a runaway buffer is clamped at input (<= k_max_field_bytes, UTF-8
// safe), the widget swallows further keystrokes at the cap, and the whole form
// still renders bounded and clean.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(oversized_buffer_is_bounded)
{
    const Theme& theme = theme_for(ThemeName::dark);
    FormModel model(link_create_info());
    const std::size_t ti = index_of(model, "target_url");

    model.set_text(ti, std::string(70u * 1024u, 'x'));  // > 64 KiB
    BOOST_TEST(model.fields()[ti].text.size() <= FormModel::k_max_field_bytes);

    // Keystrokes at the cap are swallowed, so the buffer stays bounded.
    ftxui::Component widget = make_field_widget(model, ti, theme);
    for (int i = 0; i < 16; ++i) {
        widget->OnEvent(ftxui::Event::Character("y"));
    }
    BOOST_TEST(model.fields()[ti].text.size() <= FormModel::k_max_field_bytes);

    // Rendering stays bounded (no line wider than the terminal) and
    // hazard-free.
    FormHarness h(ThemeName::dark);
    FormViewContext ctx = h.context("New link");
    const std::string grid =
        render_twice(make_form_component(model, ctx), 80, 24);
    for (const std::string& line : split_lines(grid)) {
        BOOST_TEST(url_shortener::tui::display_width(line) <= 80u);
        BOOST_TEST(!has_display_hazard(line));
    }
}
