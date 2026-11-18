#pragma once

#include <string>
#include <vector>

#include <url_shortener/tui/keymap.hpp>

namespace url_shortener::tui
{

/**
 * @brief One row of the F1 help overlay: a key binding and what it does.
 *
 * The flattened, de-duplicated form of a @ref KeyHint produced by
 * @ref HelpModel::entries. Both fields are rendered through
 * `sanitize_for_terminal` at the overlay render sink (T-110), so even a hint a
 * screen contributes from server-derived text cannot inject terminal controls.
 */
struct HelpEntry
{
    std::string keys;   ///< The key or key combination, e.g. `"j/k"`, `"Enter"`.
    std::string label;  ///< The action it performs, e.g. `"move"`, `"open"`.
};

/**
 * @brief The pure (FTXUI-free) model of the F1 help overlay.
 *
 * Combines the always-present global keymap with the active screen's own hints
 * into a single ordered, de-duplicated list the overlay renders. Keeping it a
 * plain value type lets the overlay content be asserted with no terminal (ADR
 * 0003 rule 6) and keeps the model library FTXUI-free (contract K2).
 */
class HelpModel
{
public:
    /**
     * @brief Builds the model from the global keymap and the active screen's
     * hints.
     *
     * @param global The global keymap hints shown first (e.g. help, palette,
     * back, quit); moved in.
     * @param screen The active screen's own hints shown after the globals;
     * moved in. May be empty for a screen with no hints of its own.
     */
    HelpModel(std::vector<KeyHint> global, std::vector<KeyHint> screen);

    /**
     * @brief Returns the overlay rows in display order.
     *
     * Global entries come first (in the order given), then the screen-specific
     * entries (in the order given). Exact duplicates - an entry whose `keys`
     * and `label` both match one already emitted - are dropped, keeping the
     * first occurrence, so a screen that re-lists a global key does not show it
     * twice. The result is stable and contains no @ref KeyHint twice.
     *
     * @return The de-duplicated help rows; empty only when both input lists are
     * empty.
     */
    std::vector<HelpEntry> entries() const;

private:
    ///< The global keymap hints, rendered first.
    std::vector<KeyHint> m_global_;

    ///< The active screen's own hints, rendered after the globals.
    std::vector<KeyHint> m_screen_;
};

}  // namespace url_shortener::tui
