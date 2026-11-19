/**
 * @file app_navigation.cpp
 * @brief Implements the headless-testable navigation primitives declared in
 * @ref url_shortener/tui/app_navigation.hpp (BUG-2, 06.0/20 smoke).
 */
#include <cstddef>

#include "url_shortener/tui/app_navigation.hpp"

namespace url_shortener::tui
{

namespace
{

/// Returns the index of the first hosted screen whose id is @p id, or `-1` when
/// none matches (so an unregistered tab is handled without a bogus 0 default).
int index_of_screen(const std::vector<std::unique_ptr<IScreen>>& screens,
                    ScreenId id)
{
    for (std::size_t i = 0; i < screens.size(); ++i) {
        if (screens[i]->id() == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

int sync_active_screen(const std::vector<std::unique_ptr<IScreen>>& screens,
                       Router& router,
                       ScreenContext& context,
                       std::optional<ScreenId>& entered_tab)
{
    const ScreenId active = router.active_tab();
    const int active_index = index_of_screen(screens, active);

    if (!entered_tab.has_value() || *entered_tab != active) {
        if (entered_tab.has_value()) {
            const int prev = index_of_screen(screens, *entered_tab);
            if (prev >= 0) {
                screens[static_cast<std::size_t>(prev)]->on_leave();
            }
        }
        // `text_input_active` must reflect only the screen now becoming active
        // (BUG-2): clear any flag an off-screen screen may have left set, then
        // let the entering screen re-assert its own text focus in on_enter.
        context.text_input_active = false;
        if (active_index >= 0) {
            screens[static_cast<std::size_t>(active_index)]->on_enter();
        }
        entered_tab = active;
    }

    return active_index;
}

GlobalKeyOutcome handle_global_key(const ftxui::Event& event,
                                   Router& router,
                                   const ScreenContext& context,
                                   bool& help_visible)
{
    using namespace ftxui;

    // While the help overlay is open it owns all input: Esc/F1/`?` close it,
    // everything else is swallowed so no key leaks to the screen beneath.
    if (help_visible) {
        if (event == Event::Escape || event == Event::F1
            || event == Event::Character("?"))
        {
            help_visible = false;
        }
        return {true, false};
    }
    // F1 opens help from anywhere, including while a text Input is focused
    // (checked before the text-input guard below); `?` stays typable.
    if (event == Event::F1) {
        help_visible = true;
        return {true, false};
    }
    // A focused text input swallows navigation keys (typing `1` in a slug field
    // must not switch tab). A form/filter screen mirrors its focus into
    // ScreenContext::text_input_active.
    if (context.text_input_active) {
        return {false, false};
    }
    if (event == Event::CtrlC) {
        return {true, true};
    }
    if (event == Event::Escape) {
        return {router.pop(), false};
    }
    if (event.is_character() && event.character().size() == 1) {
        const char key = event.character()[0];
        if (key == '?') {
            help_visible = true;
            return {true, false};
        }
        if ((key >= '1' && key <= '7') || key == ':') {
            return {router.navigate_tab(key), false};
        }
        if (key == 'q') {
            return {router.pop(), false};
        }
    }
    return {false, false};
}

}  // namespace url_shortener::tui
