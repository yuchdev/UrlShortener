#pragma once

#include <string>
#include <vector>

namespace url_shortener::tui
{

/**
 * @brief One footer / help-overlay hotkey hint: the key(s) and what they do.
 *
 * A pure value type rendered by the D1 key-hint footer (this subtask) and later
 * reused by the Help overlay (subtask 05). Both strings are composition-fixed
 * UI labels, never server data, so they need no terminal sanitization (T-110).
 */
struct KeyHint
{
    ///< The key or key combination to show, e.g. `"j/k"`, `"Enter"`, `"Ctrl-C"`.
    std::string keys;

    ///< The one-word action the key performs, e.g. `"move"`, `"quit"`.
    std::string label;
};

/**
 * @brief The always-present footer hints shown on every screen (D1).
 *
 * These are the global navigation keys the frame appends after each screen's
 * own hints: back (`q`/`Esc`) and quit (`Ctrl-C`). A screen with no hints of
 * its own (e.g. a placeholder) shows just these.
 *
 * @return The global hints in display order: `q` back, then `Ctrl-C` quit.
 */
std::vector<KeyHint> global_hints();

}  // namespace url_shortener::tui
