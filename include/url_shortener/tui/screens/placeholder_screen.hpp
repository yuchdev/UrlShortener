#pragma once

#include <memory>
#include <string>

#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>

namespace url_shortener::tui
{

/**
 * @brief Builds the body element shown by a placeholder screen.
 *
 * A single centered line used until a real screen is registered (subtasks
 * 11-17, 21) and permanently for the Analytics tab (ADR 0003 non-goal). Also
 * used directly by the frame render test as a stand-in body.
 *
 * @param message The composition-fixed text to show (never server data).
 * @return The placeholder body element; never null.
 */
ftxui::Element placeholder_body(const std::string& message);

/**
 * @brief Creates a placeholder @ref IScreen for a tab that has no real screen
 * yet.
 *
 * Registered by @ref App for the Analytics and Settings tab slots and the
 * not-yet-implemented screens so navigation and the D1 frame work before those
 * subtasks land. It has no hints and makes no command calls.
 *
 * @param id The screen the placeholder stands in for.
 * @param message The body text to render (composition-fixed).
 * @return An owning @ref IScreen that renders @p message; never null.
 */
std::unique_ptr<IScreen> make_placeholder_screen(ScreenId id,
                                                 std::string message);

}  // namespace url_shortener::tui
