#include "url_shortener/tui/keymap.hpp"

namespace url_shortener::tui
{

std::vector<KeyHint> global_hints()
{
    return {KeyHint {"q", "back"}, KeyHint {"Ctrl-C", "quit"}};
}

}  // namespace url_shortener::tui
