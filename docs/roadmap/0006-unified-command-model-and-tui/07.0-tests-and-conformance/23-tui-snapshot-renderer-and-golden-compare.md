# 23 - TUI snapshot renderer and golden compare (06.0 harness, mostly consumed)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22
**Blocks:** 24, 25, 27, 28, 29, 08.0/11
**Estimated size:** XS (reduced from M: the harness shipped in 06.0/01)
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** The render harness is
> **shipped and working**: `render_helpers.hpp` (`render_to_text(Element|
> Component, w=80, h=24)`, `send_events(component, vector<Event>)`), `golden.hpp`
> (`assert_matches_golden`, `text_diff`, `split_lines`, env `UPDATE_GOLDEN`,
> compile def `TUI_GOLDEN_DIR`) and `inline_poster.hpp` (`InlinePoster::run_pending`
> = the originally-specced "drain"). 70 goldens are committed and green. So this
> subtask no longer rewrites the harness. **Removed APIs that do not exist and
> contradict as-built:** `to_ascii_borders` / "ASCII borders only" (the committed
> goldens use **box-drawing glyphs**, read straight from `Cell::character`); the
> `# golden: <name>` header line (goldens have no header); the `CI=true` update
> guard; the token-form `press_keys`; `drain(MockCommandClient&, root)`; a
> headless `App` factory and a fake clock (neither exists - `App::run()` starts
> the loop; screens are driven directly via `send_events`/`OnEvent`). The only
> gap is an **optional** token helper placed in `tui_test_support.hpp` (subtask
> 22), never in the shipped `render_helpers.hpp`.

## Coverage already delivered (06.0/01, 06.0/03)

| Original 07.0 need | Delivered by (consumed) |
|---|---|
| `render_to_text`, `send_events` | `tests/unit/tui/support/render_helpers.hpp` |
| `assert_matches_golden`, `text_diff`, `UPDATE_GOLDEN`, `TUI_GOLDEN_DIR` | `tests/unit/tui/support/golden.hpp` |
| async drain for headless settle | `tests/unit/tui/support/inline_poster.hpp` (`run_pending`) |
| harness self-tests | `tui__01_render_harness_selftest`, `tui__05_ascii_test` |
| 70 committed goldens (`d01`..`d13`, `table_*`, etc.) | the `tui__04`..`tui__21` screen tests |

## Dependencies

- Subtask 22 (`tui_test_support.hpp` helpers); Task 06.0/01 (shipped helpers, `TUI_GOLDEN_DIR`, `register_tui_tests`, `TUI_RENDER_UNIT_SOURCES`), 06.0/03 (`InlinePoster`).
- FTXUI APIs (as pinned by 06.0/01): `Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h))`, `Render(Screen&, Element)`, `PixelAt(x, y).character`, `Component::Render()`, `Component::OnEvent(Event)`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/support/tui_test_support.hpp` | modify (subtask 22's header) | **optional** token helper `press_keys(Component, {"j","Enter",...})` mapping tokens to `ftxui::Event` and calling `send_events` - a convenience over the shipped `send_events`, test-support only, justified by the state-machine test (27). Omit if 27 uses explicit `Event`s. |
| `.gitattributes` | modify or create | `tests/unit/tui/golden/* text eol=lf` (if not already set by 06.0) |

No change to `render_helpers.hpp` or `golden.hpp` (shipped, used by 57 tests).

## Classes and functions

```cpp
namespace url_shortener::tui::testing {   // shipped 06.0/01 helpers, unchanged:
//   std::string render_to_text(ftxui::Element|ftxui::Component, int w=80, int h=24);
//   void send_events(ftxui::Component, const std::vector<ftxui::Event>&);
//   void assert_matches_golden(const std::string& actual, const std::string& golden_rel_path);
//   std::string text_diff(const std::string& expected, const std::string& actual);

// Optional convenience (subtask 22 header, not render_helpers.hpp):
void press_keys(ftxui::Component root,
                std::initializer_list<std::string_view> keys);  // tokens -> Event -> send_events
}
```

Determinism as actually shipped: fixed `Screen::Create(w,h)`; text read from
`Cell::character` only (no ANSI); right-trimmed rows; box-drawing glyphs kept.
Themes include `mono` (06.0/05); time-dependent columns are fed by scripted mock
responses (there is no fake clock in `include/url_shortener/tui`).

## Implementation steps

1. If subtask 27 needs it, add `press_keys` to `tui_test_support.hpp` (header-only) and self-test the token mapping there. Otherwise this subtask is a no-op beyond the `.gitattributes` check.
2. Confirm the shipped `tui__01`/`tui__05` self-tests still pass; do not modify the shipped helpers.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__(01|05)_" --output-on-failure
UPDATE_GOLDEN=1 ctest --test-dir cmake-build -R "^tui__" --output-on-failure    # POSIX, intentional regen only
```

## Tests

No new render test file; the `press_keys` helper (if added) is self-tested inside
`tui__22_mock_support_helpers_test` (subtask 22).

## Constraints

- Do not modify the shipped `render_helpers.hpp` / `golden.hpp` / `inline_poster.hpp`.
- One golden directory (`tests/unit/tui/golden/`), one env var (`UPDATE_GOLDEN`).
- Goldens are committed text with box glyphs; review diffs like code; `\n` line endings.

## Success criteria

1. The shipped harness and 70 goldens stay green; no second renderer/golden helper is added (grep).
2. If `press_keys` is added, its token mapping is self-tested.

## Out of scope / risks

- A `CI=true` update guard and a `# golden:` header were specced but not built; if desired, raise them as a 08.0 follow-up, not a 07.0 gate (the goldens are already deterministic).
