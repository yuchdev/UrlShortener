#pragma once

#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

/**
 * @file render_helpers.hpp
 * @brief Header-only render helpers shared by every TUI snapshot/render test.
 *
 * These helpers turn an FTXUI `Element`/`Component` into a plain-text grid so
 * layouts can be asserted against golden files without a real terminal (ADR
 * 0003 rule 6). The produced text contains **no** SGR/ANSI escape sequences:
 * it reads only the character payload of each screen cell, never
 * `Screen::ToString()` (which embeds colour codes). Later subtasks compare the
 * output with files under `tests/unit/tui/golden/` via `assert_matches_golden`
 * in `golden.hpp`.
 */

namespace url_shortener::tui::testing
{

/**
 * @brief Render an FTXUI element into a fixed-size screen and return its text.
 *
 * The element is laid out into an off-screen `ftxui::Screen` of exactly
 * @p width by @p height cells; the returned string is the grid read row by
 * row. Each row is the concatenation of its cells' `character` payloads with
 * trailing spaces trimmed; rows are joined with `'\n'`. Trailing blank rows
 * are kept so the vertical extent of a layout is visible in the golden. The
 * result is guaranteed free of ESC (0x1B) and other SGR bytes because only
 * `Cell::character` is read.
 *
 * @param element The element to render. Taken by value (FTXUI `Element` is a
 *                `shared_ptr<Node>`); the caller's node is left unchanged.
 * @param width   Target width in cells. Values below 0 are clamped to 0; 0 and
 *                1 are rendered without crashing (empty/degenerate grids).
 * @param height  Target height in cells. Clamped and tolerated like @p width.
 * @return The rendered grid as newline-joined, right-trimmed rows. Empty when
 *         @p width or @p height clamps to 0.
 */
inline std::string
render_to_text(ftxui::Element element, int width = 80, int height = 24)
{
    const int w = width < 0 ? 0 : width;
    const int h = height < 0 ? 0 : height;

    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(w), ftxui::Dimension::Fixed(h));
    ftxui::Render(screen, element);

    std::string out;
    for (int y = 0; y < screen.dimy(); ++y)
    {
        std::string row;
        for (int x = 0; x < screen.dimx(); ++x)
        {
            // Cell::character is the UTF-8 grapheme only - no style/colour.
            const std::string& ch = screen.PixelAt(x, y).character;
            row += ch.empty() ? std::string(" ") : ch;
        }
        // Trim trailing spaces so goldens are insensitive to right padding.
        const std::string::size_type last = row.find_last_not_of(' ');
        if (last == std::string::npos)
        {
            row.clear();
        }
        else
        {
            row.erase(last + 1);
        }
        if (y != 0)
        {
            out += '\n';
        }
        out += row;
    }
    return out;
}

/**
 * @brief Render a component variant by rendering its current `Render()` output.
 *
 * Equivalent to `render_to_text(component->Render(), ...)`. Use after
 * `send_events` to snapshot a component that has processed input.
 *
 * @param component The component to render. Must not be null.
 * @param width     Target width in cells (clamped like the element overload).
 * @param height    Target height in cells (clamped like the element overload).
 * @return The rendered grid, same shape and guarantees as the element overload.
 */
inline std::string
render_to_text(ftxui::Component component, int width = 80, int height = 24)
{
    return render_to_text(component->Render(), width, height);
}

/**
 * @brief Feed a sequence of events to a component, in order.
 *
 * Each event is delivered via `ftxui::ComponentBase::OnEvent`; the component
 * mutates its own state in place. Re-snapshot the result with the component
 * overload of `render_to_text`. The return value of `OnEvent` (whether the
 * event was handled) is intentionally ignored - tests assert on the rendered
 * result, not on handling flags.
 *
 * @param component The component to drive. Must not be null.
 * @param events    Events to deliver, e.g. `ftxui::Event::Character("j")` or
 *                  `ftxui::Event::Return`. An empty vector is a no-op.
 */
inline void
send_events(ftxui::Component component,
            const std::vector<ftxui::Event>& events)
{
    for (const ftxui::Event& event : events)
    {
        component->OnEvent(event);
    }
}

}  // namespace url_shortener::tui::testing
