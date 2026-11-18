#pragma once

#include <vector>

#include <url_shortener/tui/screen_id.hpp>

namespace url_shortener::tui
{

/**
 * @brief The pure screen-navigation state machine of D11 (ADR 0003).
 *
 * Holds a stack of screens whose top is the active screen, plus the legal-edge
 * table of D11 as its single source of truth (@ref is_legal). It is a plain
 * value type with no FTXUI, no clients and no I/O, so the whole navigation
 * model is unit-tested headlessly (ADR 0003 rule 6). A @ref ScreenContext hands
 * one `Router&` to every screen; the frame reads @ref active_tab to highlight
 * the D1 tab bar.
 *
 * Navigation vocabulary:
 * - **Tabs** (`[1]..[7]`, `[:]`) are global: @ref navigate_tab is legal from
 *   any screen and resets the stack to that tab root.
 * - **Pushes** follow the D11 edges only: @ref push accepts a transition iff
 *   @ref is_legal, so an illegal edge can never change the state.
 * - **Pop** is the `q` / `Esc` back action; it is a no-op at a tab root.
 * - **Confirm** is a modal: it is pushed over a form but, for the tab
 *   highlight, @ref active_tab reports the screen the confirm returns to (its
 *   recorded `return_to`), not `confirm` itself. Resolving the confirm via
 *   @ref push to that return target replaces the stack with the single tab
 *   root, so the modal leaves no residue (ADR 0003 D11 `Confirm --> Links` /
 *   `Confirm --> Dashboard`).
 *
 * Thread-safety: not synchronized; a `Router` is owned and mutated only on the
 * UI thread.
 */
class Router
{
public:
    /**
     * @brief Constructs a router sitting at the Dashboard tab root.
     *
     * Mirrors the D11 initial edge `[*] --> Dashboard`: the stack is
     * `{dashboard}` and @ref current / @ref active_tab both report
     * @ref ScreenId::dashboard.
     */
    Router();

    /**
     * @brief Returns the active screen (the top of the stack).
     *
     * @return The screen currently shown; never changes to an illegal value.
     */
    ScreenId current() const;

    /**
     * @brief Returns the full screen stack, bottom (tab root) to top (active).
     *
     * @return A reference to the live stack, valid until the next mutating
     * call; always non-empty (its front is the current tab root).
     */
    const std::vector<ScreenId>& stack() const;

    /**
     * @brief Returns the tab that should be highlighted for the current screen.
     *
     * For a nested screen (e.g. Link detail) this is the owning tab (Links);
     * for the Confirm modal it is the recorded return target, so the tab bar
     * never highlights `confirm`. The result is always one of the eight D1 tabs.
     *
     * @return The owning tab @ref ScreenId of the active screen.
     */
    ScreenId active_tab() const;

    /**
     * @brief Switches to a tab, clearing any nested screens.
     *
     * Accepts the D1 tab keys: `'1'` Dashboard, `'2'` Links, `'3'` Tables,
     * `'4'` Import/Export, `'5'` Jobs, `'6'` Analytics, `'7'` Settings, and
     * `':'` Palette. On a recognized key the stack is reset to the single tab
     * root (so a `q`/back afterwards is a no-op), matching "tabs are global".
     *
     * @param key The pressed key; one of `'1'..'7'` or `':'`.
     * @return `true` and resets the stack when @p key is a tab key; `false` and
     * leaves the state unchanged for any other character.
     */
    bool navigate_tab(char key);

    /**
     * @brief Pushes a transition if the D11 edge table allows it.
     *
     * When @ref current is @ref ScreenId::confirm and @p next is a legal
     * confirm return target, the modal is resolved: the stack is replaced with
     * `{next}` (a tab root). Otherwise a legal edge appends @p next to the
     * stack. Pushing @ref ScreenId::confirm records the return target (Links
     * for a link-form confirm, Dashboard for a palette/generic-form confirm).
     *
     * @param next The screen to transition to.
     * @return `true` when `is_legal(current(), next)` and the state advanced;
     * `false` and no change when the edge is illegal.
     */
    bool push(ScreenId next);

    /**
     * @brief Pops back to the previous screen (`q` / `Esc`).
     *
     * @return `true` when a nested screen was popped; `false` and no change
     * when already at a tab root (the stack holds a single screen).
     */
    bool pop();

    /**
     * @brief The D11 edge table: whether @p to may be pushed from @p from.
     *
     * This is the one encoding of the D11 mermaid diagram's push edges (tab
     * transitions are handled by @ref navigate_tab, not here). It is `static`
     * and side-effect-free so tests can assert it exhaustively over all
     * @ref k_screen_id_count squared pairs.
     *
     * @param from The source screen.
     * @param to The candidate destination screen.
     * @return `true` iff `from -> to` is a legal D11 push edge.
     */
    static bool is_legal(ScreenId from, ScreenId to);

private:
    ///< The screen stack; front is the tab root, back is @ref current. Never
    ///< empty.
    std::vector<ScreenId> m_stack_;

    ///< Tab the Confirm modal returns to; read by @ref active_tab while
    ///< @ref current is @ref ScreenId::confirm. Set when confirm is pushed.
    ScreenId m_confirm_return_ = ScreenId::dashboard;
};

}  // namespace url_shortener::tui
