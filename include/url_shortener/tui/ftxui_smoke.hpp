#pragma once

#include <ftxui/dom/elements.hpp>

namespace url_shortener::tui
{

/**
 * @brief Build a minimal FTXUI element used to prove the render pipeline links.
 *
 * Returns a bordered `window("smoke", text("ok"))`. The element carries no
 * business data and takes no input: its sole purpose is to give subtask 01's
 * render harness a deterministic, dependency-light subject (the golden
 * `00_smoke_20x3.txt`) and to prove that `url_shortener_tui` links FTXUI's
 * `screen`/`dom` libraries. Later subtasks render real screens instead.
 *
 * @return A freshly constructed FTXUI `Element`; never null. The returned node
 *         is owned by the caller (FTXUI `Element` is a `shared_ptr<Node>`).
 */
ftxui::Element render_smoke_element();

}  // namespace url_shortener::tui
