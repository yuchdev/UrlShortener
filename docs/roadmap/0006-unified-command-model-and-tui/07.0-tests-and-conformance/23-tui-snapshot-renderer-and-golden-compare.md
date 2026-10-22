# 23 - TUI snapshot renderer and golden compare (extension of the 06.0 harness)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22
**Blocks:** 24, 25, 27, 28, 29, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Make the render harness that Task 06.0 subtask 01 ships
(`tests/unit/tui/support/render_helpers.hpp`: `render_to_text`, `send_events`;
`tests/unit/tui/support/golden.hpp`: `assert_matches_golden`, env
`UPDATE_GOLDEN=1`) strong enough for the whole snapshot suite: deterministic
80x24 mono/ASCII frames, readable diffs on mismatch, a safe "update goldens"
workflow, key-sequence and async-drain helpers. So layout regressions are
caught without a terminal (ADR 0003 rule 6, diagrams D1-D10 as the visual spec).

This subtask **extends** the 06.0 helpers in place; it does not create a second
renderer, a second golden directory or a second environment variable.

## Dependencies

- Subtask 22 (extended mock); Task 06.0 subtask 01 (helpers, `TUI_GOLDEN_DIR` compile definition, `register_tui_tests`,
  `TUI_RENDER_UNIT_SOURCES`).
- FTXUI APIs used (VERIFY AT PIN TIME against the version pinned by 06.0 subtask 01): `ftxui::Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h))`, `ftxui::Render(Screen&, Element)`, `Screen::PixelAt(x, y).character`, `Component::Render()`, `Component::OnEvent(Event)`, `Terminal::SetColorSupport`.
- 06.0 must expose: a factory building the `App` root component from `(ICommandClient&, TuiOptions)` without starting `ScreenInteractive::Loop` (06.0/04), an injectable time source for clocks/"started" columns, and the theme option `mono` (06.0/05).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/support/render_helpers.hpp` | modify | add `press_keys`, `drain`, ASCII border mapping and determinism setup (below) |
| `tests/unit/tui/support/golden.hpp` | modify | `assert_matches_golden` prints a unified diff (first 40 lines) and refuses `UPDATE_GOLDEN` when `CI=true`; rewrite restricted to `TUI_GOLDEN_DIR` |
| `tests/unit/tui/24_snapshot_harness_test.cpp` | create | harness self-test with a tiny fake component (Task 07.0 owns `tests/unit/tui/20-69`) |
| `sources.cmake` | modify | append the test to `TUI_RENDER_UNIT_SOURCES` (owned by 06.0/01) |
| `.gitattributes` | modify or create | `tests/unit/tui/golden/* text eol=lf` |

## Classes and functions

```cpp
namespace url_shortener::tui::testing {   // namespace of the 06.0/01 helpers

// Existing (06.0/01): render_to_text(Element|Component, w = 80, h = 24), send_events, assert_matches_golden.

// Added: token form of send_events: "j", "k", "Enter", "Esc", "Tab", "Ctrl-C" tokens or literal text
// ("hello" => five Character events). Returns after each event processed.
void press_keys(ftxui::Component root, std::initializer_list<std::string_view> keys);
// Deliver queued worker responses through the 06.0/03 InlinePoster so async screens settle.
void drain(MockCommandClient&, ftxui::Component root);
// Applied inside render_to_text: maps box-drawing glyphs to '+', '-', '|' so goldens match ADR style.
std::string to_ascii_borders(std::string text);
}
```

Golden file format: plain UTF-8 text; first line `# golden: <name>  (80x24, theme=mono, locale=C)`; rest is the frame. On mismatch, `assert_matches_golden` prints a unified diff (first 40 differing lines) and the rewrite instruction `UPDATE_GOLDEN=1 ctest -R <target>`.

Determinism rules enforced by the harness and documented in the header:

1. Theme `mono` and ASCII borders only (ADR 0003 negative consequences: legacy consoles) - goldens never contain color or box-drawing glyphs that differ per terminal.
2. `Terminal::SetColorSupport(Palette1)`; `setlocale(LC_ALL, "C")`.
3. All times from the injected fake clock (`2026-10-19T10:44:02Z`); no `now()` in goldens.
4. Terminal size fixed by `Screen::Create`, never read from the OS.
5. Wide/CJK glyph handling: tested once in the self-test (cell width > 1) to keep column math honest.

## Implementation steps

1. Extend `render_to_text` with the ASCII mapping (still through `Screen::PixelAt`, not `ToString`, which emits ANSI).
2. Extend `assert_matches_golden` with the diff, the CI guard and the canonical-prefix check on the rewrite path.
3. Add `press_keys` and `drain`.
4. Self-test: render a hand-built element to a known string; mismatch produces a diff; update mode writes into a temp golden dir (the self-test overrides the dir via env `TUI_GOLDEN_DIR_OVERRIDE`); the 06.0 self-test (`tui__01_render_harness_selftest`) still passes.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__(01|24)_" --output-on-failure
UPDATE_GOLDEN=1 ctest --test-dir cmake-build -R "^tui__(2[0-2]|3[0-3])_" --output-on-failure    # POSIX
# PowerShell: $env:UPDATE_GOLDEN=1; ctest --test-dir cmake-build -C Debug -R "^tui__(2[0-2]|3[0-3])_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/24_snapshot_harness_test.cpp` | `tui__24_snapshot_harness_test` | `unit` | render fixed element; right-trim; ASCII mapping; golden match; golden mismatch diff; update mode writes only inside golden dir; `CI=true` + `UPDATE_GOLDEN` fails; wide glyph width; `press_keys` token parsing |

## Constraints

- `UPDATE_GOLDEN` must never be set in CI; the harness fails if `CI=true` and `UPDATE_GOLDEN` is set.
- One environment variable (`UPDATE_GOLDEN`, 06.0 README K4) and one golden directory (`tests/unit/tui/golden/`).
- Goldens are committed text; review diffs like code.
- Line endings: goldens stored with `\n` (`.gitattributes`).

## Success criteria

1. Two consecutive runs on Windows and Linux produce identical text for the same component.
2. A one-character layout change in a screen fails with a readable diff.
3. Update mode cannot write outside the golden directory.
4. There is exactly one renderer/golden helper set under `tests/unit/tui/support/` (grep).

## Out of scope / risks

- Pixel-perfect color/style regression: not covered (mono text only).
- Risk: FTXUI version differences change border glyph widths; goldens pin the FTXUI version selected by 06.0 and must be regenerated on upgrade.
