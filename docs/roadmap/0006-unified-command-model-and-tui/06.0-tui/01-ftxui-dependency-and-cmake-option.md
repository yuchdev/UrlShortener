# 01 - FTXUI dependency, CMake option and render test harness

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** none inside 06.0 (build-only; can start before 03.0/05.0 land)
**Blocks:** 02, 03, 04, 19, 20 (directly; every later subtask links FTXUI or the TUI test helpers through these)
**Estimated size:** M
**Suggested agent:** cpp-expert (CMake), testing-expert (harness)

## Objective

Verify and pin one FTXUI release, add it behind the CMake option
`URLSHORTENER_ENABLE_TUI` (default `ON`, decision-log Q11), create the two TUI
library targets, and ship the test helpers every later render/snapshot test
uses (render an `ftxui::Element`/`Component` to a plain-text grid and compare it
to a golden file). This is the "verify and pin" step that
[decision-log.md](/docs/roadmap/0006-unified-command-model-and-tui/decision-log.md)
and [ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) Decision 1
defer to implementation time. Renders no diagram itself.

## Dependencies

- External: FTXUI (`ArthurSonzogni/FTXUI`, MIT). Candidate: latest stable tag that
  is at least two weeks old - **VERIFY AT PIN TIME** (release number, that the
  tag builds with `CMAKE_CXX_STANDARD 17` on MSVC 19.3x, clang and gcc, and the
  vcpkg port name `ftxui` and its version in baseline `32305df7...` of `vcpkg.json`).
- Internal: none. Uses the existing `FetchContent` pattern of `CMakeLists.txt`
  (`yaml-cpp`, `hiredis`) and `cmake/FetchSOCI.cmake`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `cmake/FetchFTXUI.cmake` | create | `fetch_ftxui()` function: `find_package(ftxui CONFIG QUIET)` first, else `FetchContent_Declare(ftxui GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git GIT_TAG <pinned> GIT_SHALLOW TRUE)`; forces `FTXUI_BUILD_EXAMPLES/DOCS/TESTS/MODULES OFF`, `FTXUI_ENABLE_INSTALL OFF` (option names VERIFY AT PIN TIME) |
| `CMakeLists.txt` | modify | `option(URLSHORTENER_ENABLE_TUI "Build the terminal UI (FTXUI)" ON)`; when ON: `include(cmake/FetchFTXUI.cmake)`, `fetch_ftxui()`, define targets `url_shortener_tui_model` and `url_shortener_tui` (see Classes), `target_compile_definitions(url_shortener PRIVATE URLSHORTENER_WITH_TUI=1)` and link `url_shortener_tui`; when OFF: define `URLSHORTENER_WITH_TUI=0` and nothing else; add macro `register_tui_tests(src_list link_lib)` modelled on `register_labeled_cpp_tests` (that macro links only `url_shortener_common`, so it cannot be reused) |
| `sources.cmake` | modify | new lists `TUI_MODEL_CPP_SOURCES`, `TUI_UI_CPP_SOURCES` (empty-but-declared here, later subtasks append), `TUI_MODEL_UNIT_SOURCES`, `TUI_RENDER_UNIT_SOURCES`; all inside `if(URLSHORTENER_ENABLE_TUI)` |
| `src/tui/ftxui_smoke.cpp` | create | `tui::render_smoke_element()` returns a `window("smoke", text("ok"))` - proves linking; deleted or kept as the harness self-test subject |
| `tests/unit/tui/support/render_helpers.hpp` | create | header-only test helpers (below) |
| `tests/unit/tui/support/golden.hpp` | create | `assert_matches_golden(plain_text, golden_path)`; env `UPDATE_GOLDEN=1` rewrites the file |
| `tests/unit/tui/golden/00_smoke_20x3.txt` | create | golden for the harness self-test |
| `tests/unit/tui/01_render_harness_selftest.cpp` | create | harness self-test |
| `vcpkg.json` | modify (optional) | add `ftxui` only under a vcpkg *feature* `tui` so default vcpkg installs are unchanged; FetchContent remains the fallback |
| `docs/adr/0003-tui-framework-and-architecture.md` | modify | record the pinned tag in Decision 1 (replace "pinned at implementation time") |

## Classes and functions

Targets (CMake):

- `url_shortener_tui_model` STATIC - sources `TUI_MODEL_CPP_SOURCES`; links `url_shortener_common`; **must not link FTXUI** (pure view-models, ADR 0003 rule 6).
- `url_shortener_tui` STATIC - sources `TUI_UI_CPP_SOURCES`; links `url_shortener_tui_model`, `ftxui::component`, `ftxui::dom`, `ftxui::screen` (target names VERIFY AT PIN TIME; FetchContent exposes them as `ftxui::*` aliases in recent releases).

Test helpers (NEW, namespace `url_shortener::tui::testing`, header-only):

```cpp
// Render an Element into a fixed-size Screen and return plain text (no SGR escapes),
// rows joined with '\n', trailing spaces trimmed per row, trailing blank rows kept.
std::string render_to_text(ftxui::Element element, int width = 80, int height = 24);
// Component variant: calls component->Render().
std::string render_to_text(ftxui::Component component, int width = 80, int height = 24);
// Feed events (Event::Character("j"), Event::Return, ...) then re-render.
void send_events(ftxui::Component component, const std::vector<ftxui::Event>& events);
void assert_matches_golden(const std::string& actual, const std::string& golden_relative_path);
```

FTXUI API used (VERIFY AT PIN TIME): `ftxui::Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h))`,
`ftxui::Render(Screen&, Element)`, `Screen::PixelAt(x, y).character`, `Screen::dimx()/dimy()`.
Do **not** use `Screen::ToString()` for goldens: it embeds SGR colour codes. Golden dir
resolved from compile definition `TUI_GOLDEN_DIR` (`${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/tui/golden`).

## Implementation steps

1. Clone-check the candidate tag locally; build FTXUI's own `component` library with `-DCMAKE_CXX_STANDARD=17` on MSVC and one POSIX compiler; record tag + commit in the ADR.
2. Write `cmake/FetchFTXUI.cmake`; disable examples/tests/docs so no extra targets pollute `ctest`.
3. Add the option and the two targets; confirm `-DURLSHORTENER_ENABLE_TUI=OFF` configures without any FTXUI download and `url_shortener` still links.
4. Add `register_tui_tests`: per source, target `tui__<stem>` (same `<parent_dir>__<stem>` rule as the existing macro), links `url_shortener_tui` (render tests) or `url_shortener_tui_model` (model tests), `Boost::unit_test_framework`, defines `TUI_GOLDEN_DIR`, label `unit`.
5. Implement the helpers and the self-test (render `window("smoke", text("ok"))` into 20x3, compare golden; assert helper strips colour: render `text("x") | color(Color::Red)` and expect plain `x`).
6. Document the pinned version and the update procedure (bump tag -> regenerate goldens with `UPDATE_GOLDEN=1` -> review diff) in the ADR.

## Commands

```powershell
# Windows (vcpkg mandatory per CLAUDE.md)
cmake -S . -B cmake-build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build cmake-build --target tui__01_render_harness_selftest
ctest --test-dir cmake-build -R "^tui__01_render_harness_selftest$" --output-on-failure
# option off
cmake -S . -B cmake-build-notui -G Ninja -DURLSHORTENER_ENABLE_TUI=OFF -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build cmake-build-notui
# regenerate goldens (PowerShell)
$env:UPDATE_GOLDEN = "1"; ctest --test-dir cmake-build -L unit -R "^tui__"; Remove-Item Env:UPDATE_GOLDEN
```

On multi-config generators add `-C Debug` to `ctest` and `--config Debug` to `cmake --build`.
FetchContent needs network access on first configure; vcpkg users may instead
`vcpkg install ftxui` and rely on `find_package`.

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/01_render_harness_selftest.cpp` | `tui__01_render_harness_selftest` | `unit` | smoke window equals golden; colour is stripped; width/height clamp (0x0 and 1x1 do not crash); golden mismatch reports a readable diff (test by comparing against a deliberately different in-memory string via a helper that returns the diff); `UPDATE_GOLDEN` unset by default |

Manual: configure twice (ON/OFF) and confirm only the ON tree has `tui__*` tests in `ctest -N`.

## Constraints

- C9: FTXUI is the only new dependency and is optional; no other third-party addition; C++17 only.
- All new `.cpp` appear in `sources.cmake` (CLAUDE.md); nothing under `src/tui/` is added to `COMMON_CPP_SOURCES`.
- `url_shortener_tui_model` stays FTXUI-free so model tests run even when the render library is troublesome on a platform.
- Pin an exact tag, never a branch.

## Success criteria

1. `cmake -DURLSHORTENER_ENABLE_TUI=OFF` configures and builds with no FTXUI fetch and all existing tests unchanged.
2. `ON` build produces `url_shortener_tui_model` and `url_shortener_tui` and `tui__01_render_harness_selftest` passes.
3. The pinned tag and verification date are recorded in ADR 0003 Decision 1.
4. `render_to_text` output contains no ESC (0x1B) characters.
5. Existing `ctest -L unit` count is unchanged except for the added `tui__*` tests.

## Out of scope / risks

- Legacy conhost behaviour is checked manually in [20](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/20-manual-smoke-checklist.md), not here.
- Risk: FTXUI's CMake option names or target aliases differ at the chosen tag - hence the VERIFY markers; fix in `FetchFTXUI.cmake` only.
- Risk: MSVC `/permissive-` + `/W4` (flags-windows preset) warnings inside FTXUI headers; include FTXUI as `SYSTEM` imported targets to keep project warnings clean.
