# 05 - Theme (dark, light, mono/ASCII) and help overlay

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/04
**Blocks:** 06, 08, 10, 11, 14, 17, 19, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Provide a semantic `Theme` (no raw colours in screens), the three themes of the
launch line `--theme dark|light|mono` (ADR 0003 "Launching"), the ASCII
fallback required by ADR 0003 Negative consequences ("`--theme mono`/ASCII-borders
fallback" for legacy consoles and tmux), and the F1 help overlay that lists the
global keymap plus the active screen's hints. Affects how every diagram D1-D10
is drawn; adds no new diagram. The overlay is the on-screen form of the footer
rows in D1/D3 ("j/k move  Enter open ...").

## Dependencies

- Internal: 06.0/04 (`Keymap`, `KeyHint`, `IScreen::hints`, frame renderer).
- External: FTXUI `Decorator` (`color`, `bgcolor`, `bold`, `dim`, `inverted`), `Color::Red/Green/Yellow/Blue/GrayDark/Default`, `borderStyled(BorderStyle::LIGHT/ROUNDED)` and `window`, `dbox`, `clear_under`, `center`, `size`, and for ASCII: subclassing `ftxui::NodeDecorator` from `ftxui/dom/node_decorator.hpp` to post-process pixels in `Render(Screen&)` (**VERIFY AT PIN TIME**; if the class is not public, wrap `ftxui::Renderer` and rewrite the `Screen` in a custom `ScreenInteractive` print path or use a plain `borderStyled(EMPTY)` plus text rules).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/theme.hpp` / `src/tui/theme.cpp` | create | FTXUI-free `Theme`, `StyleSpec`, `theme_for(ThemeName)`, `parse_theme_name` (model lib) |
| `include/url_shortener/tui/ascii.hpp` / `src/tui/ascii.cpp` | create | `transliterate_box_chars(std::string)` pure mapping (model lib) |
| `include/url_shortener/tui/help_model.hpp` / `src/tui/help_model.cpp` | create | `HelpModel` (model lib) |
| `src/tui/theme_ftxui.cpp` | create | `StyleSpec` -> `ftxui::Decorator`; `ascii_filter(Element)` (UI lib) |
| `src/tui/help_overlay.cpp` | create | overlay component bound to F1 (UI lib) |
| `tests/unit/tui/05_theme_test.cpp`, `05_ascii_test.cpp`, `05_help_model_test.cpp`, `05_theme_render_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d01_frame_mono_80x24.txt`, `help_overlay_80x24.txt`, `d01_frame_light_80x24.txt` | create | goldens (`light`/`dark` differ only by colour so their plain-text goldens are equal; the test asserts style flags via `Theme`, not text) |
| `sources.cmake` | modify | register sources and tests |

## Classes and functions

```cpp
enum class ThemeName { dark, light, mono };
enum class ColorId { none, red, green, yellow, blue, cyan, grey };         // NEW, FTXUI-free
struct StyleSpec { ColorId fg = ColorId::none; ColorId bg = ColorId::none; bool bold = false, dim = false, inverted = false; };
struct Theme {
    ThemeName name;
    bool ascii_only;                                                       // true for mono
    StyleSpec title, tab_active, tab_inactive, selected_row, header_row, ok, warning, error, hint, disabled, banner;
};
const Theme& theme_for(ThemeName name);
std::optional<ThemeName> parse_theme_name(const std::string& s);          // "dark"|"light"|"mono"

std::string transliterate_box_chars(const std::string& utf8);            // light/heavy/double/rounded box + block chars -> + - | #

struct HelpEntry { std::string keys, label; };
class HelpModel {
public:
    HelpModel(std::vector<KeyHint> global, std::vector<KeyHint> screen);
    std::vector<HelpEntry> entries() const;                               // global first, then screen-specific
};
// UI lib
ftxui::Decorator to_decorator(const StyleSpec& s);
ftxui::Element ascii_filter(ftxui::Element inner);                        // rewrites box-drawing pixels when Theme::ascii_only
ftxui::Component help_overlay(HelpModel model, bool* visible);            // toggled by F1 and '?'
```

Mono rules: no colour, selection shown with `inverted` + `>` marker, gauge/sparkline
block characters mapped to `#` and `-`, box characters to `+-|`; all information
(error vs ok) remains carried by words or glyphs, never by colour alone.

## Implementation steps

1. Define themes as constant tables; `dark`/`light` differ in `selected_row`, `title`, `hint` colours; `mono` sets `ascii_only = true` and zero colours.
2. Implement `transliterate_box_chars` on UTF-8 code points (U+2500-U+257F -> `-`,`|`,`+`; U+2588 and U+2593 -> `#`; U+2591 -> `-`; U+256x corners -> `+`); non-box text untouched; invalid UTF-8 passed through.
3. `ascii_filter`: a `NodeDecorator` subclass whose `Render(Screen&)` renders the child then walks the node's box and replaces `Pixel::character` through the same mapping (D1/D3/D5 borders become ASCII). Apply it once at the frame root when `Theme::ascii_only`.
4. Help overlay: `Modal`-style (`dbox({body, clear_under(center(window("Help", ...)))})`) shown while `*visible`; Esc/F1/`?` closes; swallows other events while open.
5. Frame (04) gets the theme through `ScreenContext`; replace any literal `color(...)` in 04's renderer with `to_decorator(theme.xxx)`.

## Commands

```powershell
cmake --build cmake-build --target tui__05_theme_test tui__05_ascii_test tui__05_help_model_test tui__05_theme_render_test
ctest --test-dir cmake-build -R "^tui__05_" --output-on-failure
# manual: build then run once the wiring subtask (19) exists
.\cmake-build\url_shortener.exe tui --theme mono
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `05_theme_test.cpp` | `tui__05_theme_test` | unit | `parse_theme_name` accepts three names, rejects `Dark`, empty, `solarized`; mono has no colours and `ascii_only`; every theme defines all semantic styles (no default-constructed holes except `none`) |
| `05_ascii_test.cpp` | `tui__05_ascii_test` | unit | full box set mapped; mixed text + box line; multibyte text (e.g. CJK, accents) unchanged; truncated UTF-8 sequence unchanged; idempotent |
| `05_help_model_test.cpp` | `tui__05_help_model_test` | unit | global entries precede screen entries; duplicates removed; empty screen hints OK |
| `05_theme_render_test.cpp` | `tui__05_theme_render_test` | unit | mono frame golden contains only ASCII bytes (<= 0x7E check) and matches `d01_frame_mono_80x24.txt`; help overlay golden; overlay swallows `j` while open and closes on Esc |

Manual: legacy conhost + mono; tmux with `TERM=screen`.

## Constraints

- Screens never name colours; only `Theme` roles (keeps the theme swappable; theming engine is a non-goal).
- Information must survive mono (no colour-only signalling).
- Model lib stays FTXUI-free (`Theme` uses `ColorId`).
- C++17; no new dependency.

## Success criteria

1. `--theme mono` output is pure ASCII for D1 frame, tables and dialogs (byte check in tests).
2. Switching theme changes no layout (plain-text goldens for dark/light are identical).
3. F1 opens/closes the overlay and lists at least the hints returned by the active screen.
4. Invalid `--theme` value is rejected at parse time (subtask 19 uses `parse_theme_name`).

## Out of scope / risks

- Runtime theme switching, user theme files, mouse support (non-goals in ADR 0003).
- Risk: `NodeDecorator` is not exported by the pinned FTXUI; fallback described in Dependencies.
- Risk: some terminals mis-measure wide glyphs; handled only by the mono fallback.
