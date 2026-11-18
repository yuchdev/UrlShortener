#include "url_shortener/tui/frame_view.hpp"

#include <string>
#include <utility>

#include <url_shortener/tui/keymap.hpp>

namespace url_shortener::tui
{

namespace
{

/// Joins screen hints and the global hints into the one-line footer text, e.g.
/// "q back  Ctrl-C quit". Hint labels are composition-fixed, never server data.
std::string footer_text(const std::vector<KeyHint>& screen_hints)
{
    std::vector<KeyHint> all = screen_hints;
    for (const KeyHint& hint : global_hints())
    {
        all.push_back(hint);
    }
    std::string out;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (i != 0)
        {
            out += "  ";
        }
        out += all[i].keys + " " + all[i].label;
    }
    return out;
}

/// The centered "terminal too small" placeholder shown below 80x24 (step 7).
ftxui::Element too_small_element()
{
    using namespace ftxui;
    return hbox({filler(),
                 vbox({filler(),
                       text("terminal too small (need 80x24)"),
                       filler()}),
                 filler()});
}

}  // namespace

ftxui::Element render_frame(const FrameModel& model,
                            ScreenId active_tab,
                            ftxui::Element body,
                            const std::vector<KeyHint>& footer_hints,
                            int width,
                            int height)
{
    using namespace ftxui;

    if (width < k_min_frame_width || height < k_min_frame_height)
    {
        return too_small_element();
    }

    // T-110: model.title() already sanitizes and width-fits the server-provided
    // backend label (FrameModel::title -> sanitize_for_terminal +
    // truncate_to_width), so no server control bytes reach this text() call. No
    // server data other than the backend label reaches the frame here.
    // FTXUI sizes a window title to its natural width, so [F1 Help] trails the
    // title rather than hugging the right corner; the D1 order (title first,
    // Help hint last) is preserved. Right-corner alignment is an FTXUI
    // limitation documented as a minor deviation from the D1 sketch.
    Element title_row =
        hbox({text(model.title()), filler(), text("[F1 Help]")});

    Elements tab_cells;
    for (const std::pair<std::string, bool>& tab : model.tabs(active_tab))
    {
        Element cell = text(tab.first);
        if (tab.second)
        {
            cell = cell | inverted;
        }
        tab_cells.push_back(std::move(cell));
        tab_cells.push_back(text(" "));
    }
    // The full D1 tab set is wider than 80 columns; an xframe clips the row at
    // the right edge, keeping full labels left-aligned, instead of letting the
    // hbox squeeze every label mid-word.
    Element tab_bar = hbox(std::move(tab_cells)) | xframe;

    Elements content;
    content.push_back(std::move(tab_bar));
    content.push_back(separator());
    if (std::optional<std::string> banner = model.banner())
    {
        content.push_back(text(*banner) | inverted);
    }
    content.push_back(std::move(body) | flex);
    content.push_back(separator());
    content.push_back(text(footer_text(footer_hints)));

    return window(std::move(title_row), vbox(std::move(content)));
}

}  // namespace url_shortener::tui
