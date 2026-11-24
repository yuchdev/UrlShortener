# TUI Development Guide

This guide explains how contributors add screens or command forms to the TUI, keep
business logic out of the presentation layer, and test their changes with the render
harness and golden snapshots. The authoritative design decisions are in
[ADR 0003](../adr/0003-tui-framework-and-architecture.md).

## Architecture in one page

The TUI is split into two CMake targets:

| Target | Description |
|---|---|
| `url_shortener_tui_model` | FTXUI-free pure-value models: `FormModel`, `TableModel`, `PaletteModel`, `LinksModel`, `CapabilityModel`, `sanitize_for_terminal`, `truncate_to_width`, query builders, etc. No FTXUI headers, no colour |
| `url_shortener_tui` | FTXUI-linked rendering and components: screen classes, `FormView`, `TableView`, `MutationFlow`, `App`, `AsyncCaller`, theme/toast/confirm rendering |

The boundary is enforced by the include scan in `tui__02_include_boundary_test`
(contract C8): a model file that includes an FTXUI header fails the test; a screen
that includes a repository or service header also fails.

Directory layout under `src/tui/`:

```
src/tui/
  app.cpp / app_navigation.cpp   – ScreenInteractive loop, global key routing
  router.cpp                     – tab stack and navigation
  screens/                       – one file per screen (FTXUI-linked)
    dashboard_screen.cpp
    links_screen.cpp
    tables_screen.cpp
    import_export_screen.cpp
    jobs_screen.cpp
    palette_screen.cpp
    generic_command_screen.cpp
    settings_screen.cpp
    placeholder_screen.cpp
  form_model.cpp / form_view.cpp / form_widgets.cpp  – generic form
  mutation_flow.cpp              – confirm + submit choke point
  async_caller.cpp               – off-UI-thread command executor
  capability.cpp                 – session permission gate
  text_fit.cpp                   – sanitize_for_terminal, truncate_to_width
  keymap.cpp                     – global key-hint definitions
  in_process_client.cpp          – the only file that sees CommandRegistry
  tui_entry.cpp                  – process wiring; the only file that sees composition
  ... (other model files)
```

Headers mirror the same split under `include/url_shortener/tui/`.

The architecture diagram (ADR 0003 Decision 2):

```
App (ScreenInteractive loop)
 +-- Router: screen stack, hotkeys, focus
 +-- Screens: Dashboard | Links | Tables | Import | Jobs | Palette
 +-- FormBuilder: ArgSpec[] -> FTXUI form components (generic)
 +-- TableView: paged rows -> FTXUI table, sort/filter/search
 +-- Theme, Help overlay, Toast/Confirm dialogs
         |
         v  AsyncCaller::submit(CommandRequest)
         |
  +------+------------------------------+
  | InProcessClient -> CommandRegistry  |
  | [future] RemoteClient -> REST / IPC |
  +--------------------------------------+
```

Screens see only `AsyncCaller` through `ScreenContext`. They never hold a pointer to
`ICommandClient` or `CommandRegistry`.

## Rules

ADR 0003 Decision 2 defines seven rules. Each is listed with the test that enforces it.

| # | Rule | Enforcing test |
|---|---|---|
| 1 | Only `ICommandClient` crosses out of the TUI. Screens build `CommandRequest`s, never include service/repository/registry headers. | `tui__02_include_boundary_test`, `commands__79_no_http_server_in_local_modes_test` |
| 2 | Generic first. `FormBuilder` renders any command from its `ArgSpec` list; bespoke screens are convenience layers on the same calls. | `tui__11_generic_command_flow_test` case `every_catalog_command_selectable_and_executable` |
| 3 | Never block the UI thread. Commands run on `AsyncCaller` workers; responses are posted back via `IUiPoster`. | `tui__33_nonblocking_ui_test` |
| 4 | Mutations confirm. Descriptors with `mutating = true` show a confirm dialog; `link.delete` requires typing the slug. | `tui__08_mutation_flow_test` |
| 5 | Read-only mode disables, never hides. Write controls render with the `not_authorized` reason in a `--read-only` session. | `tui__18_readonly_matrix_test` |
| 6 | Headless testability. `ICommandClient` is mockable; screens render to FTXUI `Screen` buffers without a terminal. | `tui__01_render_harness_selftest` |
| 7 | No secrets on screen. Auth/session tables and secret-named columns are excluded in the `db.*` handlers; the TUI cannot display them even if it tried. | `tui__34_no_secrets_real_registry_test` |

## Adding a command to the TUI

In almost every case: nothing to do.

Register the command descriptor in the command registry (see the
[command catalog guide](../commands/README.md)). The Command Palette discovers it
automatically at runtime via `ICommandClient::commands()`. The `FormBuilder` generates
a working form from the `ArgSpec` list. The palette entry, the form, the confirmation
dialog (when `mutating = true`), and the result viewer all appear with zero TUI code.

This is proven by `every_catalog_command_selectable_and_executable` in
`tui__11_generic_command_flow_test`: the test iterates the full catalog and verifies
that every command is both selectable from the palette and executable through the
generated form.

The only exception is a command that needs a bespoke screen for operator ergonomics
(see the next section).

## Adding a bespoke screen

A bespoke screen is a convenience layer over the generic form; it must still route
every submit through `run_mutation` (ADR 0003 rule 4).

Steps:

1. **Screen class.** Create `src/tui/screens/<name>_screen.cpp` and the matching
   header under `include/url_shortener/tui/screens/`. Implement `IScreen`
   (`include/url_shortener/tui/screen.hpp`): `id()`, `component()`, `hints()`,
   `on_enter()`, `on_leave()`. Add the new `.cpp` to `sources.cmake`.

2. **Route in the router / state machine.** Add an entry to the `ScreenId` enum
   (`include/url_shortener/tui/screen_id.hpp`) and register the screen factory in
   `src/tui/tui_entry.cpp` via `app.register_screen(...)` to replace the placeholder
   tab (or use a new tab key if the tab bar changes). Update the D11 diagram in ADR
   0003 if the new screen is a normative part of the navigation model.

3. **Hotkey table entry.** Return the screen's key hints from `hints()` as
   `std::vector<KeyHint>`. Global hints (`q`/`Esc` back, `Ctrl-C` quit) are appended
   by the frame automatically (`src/tui/keymap.cpp`).

4. **Help overlay text.** The help overlay renders all hints returned by the active
   screen. No separate registration is needed.

5. **Mock-driven snapshot test.** Add a test under
   `tests/unit/tui/<NN>_<name>_screen_test.cpp`. Use `MockCommandClient` to script
   responses, `InlinePoster` as the `IUiPoster`, and `render_to_text` to render the
   component. Add the test to the matching source list in `CMakeLists.txt`.

6. **Golden review.** Run with `UPDATE_GOLDEN=1` to generate the initial golden file
   under `tests/unit/tui/golden/`. Review the diff carefully; every subsequent run
   compares against this file. Golden files use an 80 × 24 canvas and the `mono`
   theme for determinism.

7. **ADR 0003 diagram update** (if the screen is normative): amend the D11 state
   machine diagram in the "As-built amendments" section with the new screen node and
   its edges, and record the reason per the ADR's deviation policy.

## FormBuilder and ArgSpec types

`FormModel` (`include/url_shortener/tui/form_model.hpp`) builds per-field editing
state from a `CommandInfo`'s `args` list. `FormView` / `form_widgets.cpp` renders
one FTXUI widget per `ArgKind`.

| `ArgKind` | Widget | Notes |
|---|---|---|
| `string` | Single-line text input | UTF-8 backspace handled |
| `integer` | Single-line text input | Numeric validation on submit |
| `boolean` | Checkbox | `[x]` / `[ ]` |
| `enumeration` | Radio group | Choices from `ArgInfo::enum_values` |
| `string_list` | Single-line text input | Comma-separated; split on submit |
| `string_map` | Single-line text input | `key=value,...` pairs; split on submit |
| `object` | Nested group of sub-fields | Members from `ArgInfo::members` |
| `timestamp` | Single-line text input | ISO-8601; validated on submit |

When a new `ArgKind` is added to `ICommandClient::ArgKind` (the TUI's view-model
mirror of `commands::ArgType`), the `tui__30_form_model_property_test` property
test will fail on the missing case. Add the new widget to `form_widgets.cpp` and
the test will pass again.

## Threading

The TUI has one UI thread (FTXUI's `ScreenInteractive` event loop) and a worker pool
inside `AsyncCaller` (default: 2 workers). The threading contract:

- **Submit** (`AsyncCaller::submit`): called on the UI thread; puts the request in a
  bounded queue (capacity 64) and returns immediately. A full queue answers with a
  `busy` error envelope without blocking.
- **Worker**: calls `ICommandClient::call` (which calls `CommandRegistry::execute`)
  on a background thread. `InProcessClient` is re-entrant.
- **Response delivery** (`IUiPoster::post`): the worker calls the poster; the poster
  marshals the completion task back to the UI thread. FTXUI's poster uses
  `ScreenInteractive::PostEvent`; tests use `InlinePoster::run_pending()` to drain
  synchronously.
- **Never block the UI thread**: a screen must not sleep, hold a mutex, or call a
  blocking I/O function inside `OnEvent` or `OnRender`. All such work goes through
  `AsyncCaller::submit`.

Job polling (`PollTicker` in jobs and dashboard) posts a UI-thread tick at a
configurable interval and never touches the command client directly. Completion
handlers that capture `this` must check a `std::weak_ptr` alive-guard so a late
completion after screen teardown is a no-op (threat model T-120).

## Testing

The test harness lives in `tests/unit/tui/support/`:

| File | Purpose |
|---|---|
| `mock_command_client.hpp` | `MockCommandClient`: scripted, recording `ICommandClient` for use without a registry or storage |
| `inline_poster.hpp` | `InlinePoster`: records posted tasks and runs them on demand via `run_pending()` |
| `render_helpers.hpp` | `render_to_text(component, width, height)` renders to a plain string; `send_events(component, events)` injects key events |
| `settle.hpp` | `settle_until_idle(caller, poster)` drains a live `AsyncCaller` to true quiescence |
| `golden.hpp` | `assert_matches_golden(text, name)` / `UPDATE_GOLDEN=1` workflow |
| `tui_test_support.hpp` | Convenience aggregator for the most common headers |

**Typical screen test pattern:**

```cpp
MockCommandClient client;
InlinePoster poster;
AsyncCaller caller(client, poster);
// script responses
client.enqueue("link.list", make_success("tui-1", list_json));
// build screen and render
auto screen = make_links_screen(ctx, catalog);
screen->on_enter();
settle_until_idle(caller, poster);  // drain async responses
assert_matches_golden(render_to_text(screen->component(), 80, 24),
                      "d03_links_80x24");
```

**Golden workflow.** On first run, or after an intentional layout change, set the
environment variable `UPDATE_GOLDEN=1` and run the test. The golden file under
`tests/unit/tui/golden/` is regenerated. Commit the new golden alongside the code
change. Without `UPDATE_GOLDEN=1`, any diff from the golden fails the test.

**Determinism rules.** Golden renders must use:
- `--theme mono` (ASCII borders, no ANSI colour codes)
- 80 × 24 canvas
- A fake `ManualClock` (for timestamps) or a fixed clock
- Deterministic IDs (e.g. the scripted response provides fixed `id` values)

**Form property tests** (`tui__30_form_model_property_test`) run a randomized suite
over all `link.*` descriptors to verify that every `ArgSpec` type survives a
round-trip through `FormModel`. Seed the PRNG with `FORM_PROP_SEED=<n>` to reproduce
a specific failure.

**Debugging a golden diff.** Run the test without `UPDATE_GOLDEN=1`; the failure
message shows a character-level diff. Common causes: a new field in the response JSON,
a column-width change, or a sanitizer stripping a character the golden expected.

**TUI conformance shim.** `tui__19_tui_entry_boundary_test` scans `tui_entry.cpp`
to verify it includes no network or Boost.Asio headers (contract C5/C8/C11). This is
the boundary between the TUI wiring and the rest of the process.

## Dependencies

The TUI uses one third-party library:

| Library | Version pin | Why |
|---|---|---|
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | v7.0.3 commit `f921fad208912747c17d129a8ef75ec7624b6eec` (verified 2026-11-18) | Zero external dependencies, first-class Windows support, declarative component model |

The pin is an immutable commit SHA (never a movable tag) in `cmake/FetchFTXUI.cmake`.
Bumping the pin:

1. Update the SHA in `cmake/FetchFTXUI.cmake` (see ADR 0003 Dependency pin policy).
2. Verify the new commit compiles as C++17 (`cxx_std_17` in FTXUI's CMake).
3. Run `UPDATE_GOLDEN=1 ctest --test-dir cmake-build -L unit` to regenerate all
   golden files.
4. Review the golden diff; any layout change must be intentional.
5. Run the [manual smoke checklist](../testing/tui_manual_smoke.md).

**Disabling the TUI build:**

```bash
cmake .. -DURLSHORTENER_ENABLE_TUI=OFF
```

Everything outside `src/tui/` compiles and runs without FTXUI. The `tui` subcommand
reports itself as unavailable.

## Checklist for a TUI PR

Before merging any PR that touches the TUI:

1. `tui__02_include_boundary_test` passes (no new service/registry/C8 include).
2. `tui__34_no_secrets_real_registry_test` passes (no secret column appears on screen).
3. All golden files updated with `UPDATE_GOLDEN=1` and diffs reviewed.
4. New `ICommandClient::ArgKind` values (if any) have a widget in `form_widgets.cpp`
   and `tui__30_form_model_property_test` passes.
5. Every mutating submit routes through `run_mutation`; no direct `caller.submit` for
   a write command.
6. No blocking call in `OnEvent` or `OnRender`; all I/O goes through `AsyncCaller`.
7. Alive-guard (`std::weak_ptr<bool>`) used in any completion that captures `this` and
   could outlive the screen.
8. [Manual smoke checklist](../testing/tui_manual_smoke.md) run on the target platform.
