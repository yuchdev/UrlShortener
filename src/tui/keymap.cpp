#include "url_shortener/tui/keymap.hpp"

namespace url_shortener::tui
{

std::vector<KeyHint> global_hints()
{
    // `q` and `Esc` are both the back action in handle_global_key (and the F1
    // help overlay lists "q / Esc"), so the footer names both keys.
    return {KeyHint {"q/Esc", "back"}, KeyHint {"Ctrl-C", "quit"}};
}

}  // namespace url_shortener::tui
