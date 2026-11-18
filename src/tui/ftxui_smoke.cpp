#include "url_shortener/tui/ftxui_smoke.hpp"

#include <ftxui/dom/elements.hpp>

namespace url_shortener::tui
{

ftxui::Element render_smoke_element()
{
    // window() defaults to a ROUNDED border in FTXUI v7; the harness golden
    // captures whatever this renders, so no border style is asserted here.
    return ftxui::window(ftxui::text("smoke"), ftxui::text("ok"));
}

}  // namespace url_shortener::tui
