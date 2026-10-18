# 04 - App shell, frame (D1) and screen router (D11)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/01, 02, 03
**Blocks:** 05, 06, 08, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
**Estimated size:** L
**Suggested agent:** cpp-expert

## Objective

Build the skeleton every screen plugs into: the `App` that owns the
`ftxui::ScreenInteractive` loop, the persistent frame of **D1** (title bar with
backend/user/Help hint, tab bar `[1]Dashboard ... [:]Palette`, body, key-hint
footer, memory-store banner), and the `Router`, a pure state machine that
implements the screen transitions of **D11**. Screens are registered through an
`IScreen` interface so later subtasks add themselves without touching the frame.

## Dependencies

- Internal: 06.0/02 (`ICommandClient`, `SessionInfo`, `json_view`), 06.0/03 (`AsyncCaller`, `FtxuiPoster`). The `system.info` command (Task 03.0 subtask 17, catalog 3.5) supplies the backend label and `persistent` flag.
- External: FTXUI `ScreenInteractive::Fullscreen()`, `ScreenInteractive::Loop(Component)`, `ScreenInteractive::ExitLoopClosure()`, `ftxui::Component`, `Renderer(Component, std::function<Element()>)`, `CatchEvent(Component, std::function<bool(Event)>)`, `Container::Tab(children, int* selected)`, `Container::Vertical`, elements `window`, `hbox`, `vbox`, `text`, `separator`, `flex`, `border`, `inverted`, `size(HEIGHT, EQUAL, n)`, `Event::Character`, `Event::F1`, `Event::Escape`, `Event::CtrlC`-style handling (Ctrl-C default behaviour and `ForceHandleCtrlC` **VERIFY AT PIN TIME**).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/screen_id.hpp` | create | `enum class ScreenId`, `to_string` |
| `include/url_shortener/tui/router.hpp` / `src/tui/router.cpp` | create | pure `Router` (model lib) |
| `include/url_shortener/tui/keymap.hpp` / `src/tui/keymap.cpp` | create | global hotkey table, `KeyHint` (model lib; reused by help overlay in 05) |
| `include/url_shortener/tui/frame_model.hpp` / `src/tui/frame_model.cpp` | create | title/banner/tab-bar text model (model lib) |
| `include/url_shortener/tui/tui_options.hpp` | create | `TuiOptions` (theme enum is forward-declared; `ThemeName` moves to this header's include list when 05 lands) |
| `include/url_shortener/tui/screen.hpp` | create | `IScreen`, `ScreenContext` (clients, caller, router, theme, toasts) |
| `include/url_shortener/tui/app.hpp` / `src/tui/app.cpp` | create | `App` (UI lib) |
| `src/tui/frame_view.cpp` | create | FTXUI rendering of D1 (UI lib) |
| `src/tui/screens/placeholder_screen.cpp` | create | "not implemented" body used until 11-17 land and for the Analytics tab |
| `tests/unit/tui/04_router_test.cpp`, `04_frame_model_test.cpp` | create | model tests |
| `tests/unit/tui/04_frame_render_test.cpp` | create | snapshot tests |
| `tests/unit/tui/golden/d01_frame_80x24.txt`, `d01_frame_memory_banner_80x24.txt`, `d01_frame_narrow_60x20.txt` | create | goldens |
| `sources.cmake` | modify | add all `.cpp` (model vs UI lists) and tests (`TUI_MODEL_UNIT_SOURCES` / `TUI_RENDER_UNIT_SOURCES`) |

## Classes and functions

```cpp
enum class ScreenId { dashboard, links, link_detail, link_form, tables, row_json, import_export,
                      jobs, job_detail, analytics, palette, generic_form, confirm };   // NEW (D11 nodes + analytics tab)

class Router {                                       // NEW, pure
public:
    Router();                                        // starts at dashboard (D11 [*] -> Dashboard)
    ScreenId current() const;
    const std::vector<ScreenId>& stack() const;
    bool navigate_tab(char key);                     // '1'..'6', ':' per D1 tab bar; clears stack to [tab]
    bool push(ScreenId next);                        // only D11-legal edges; returns false and leaves state if illegal
    bool pop();                                      // 'q' / Esc = back; no-op at a tab root
    static bool is_legal(ScreenId from, ScreenId to);// the D11 edge table, single source of truth
};

struct KeyHint { std::string keys; std::string label; };            // "j/k" "move"
std::vector<KeyHint> global_hints();                                // D1 footer defaults: "Ctrl-C quit", "q back"

class FrameModel {
public:
    FrameModel(SessionInfo session, std::string backend_label, bool persistent, std::string theme_name);
    std::string title() const;                       // "url_shortener -- backend: sqlite (links.db) -- user: local/Admin"
    std::optional<std::string> banner() const;       // "memory store - data is lost on exit" when !persistent
    std::vector<std::pair<std::string,bool>> tabs(ScreenId active) const;   // label + active flag
};

class IScreen {
public:
    virtual ~IScreen() = default;
    virtual ScreenId id() const = 0;
    virtual ftxui::Component component() = 0;
    virtual std::vector<KeyHint> hints() const = 0;  // footer for this screen
    virtual void on_enter() {}                       // refresh data via AsyncCaller
    virtual void on_leave() {}
};
// ScreenContext (NEW) carries: AsyncCaller&, SessionInfo, Router&, bool text_input_active, (const Theme& added by 05, ToastQueue& added by 06),
// std::string focus_job_id (set by Import/Export, consumed by Jobs), std::string pending_slug (Links -> LinkDetail), TuiOptions.

class App {                                          // UI lib
public:
    App(ICommandClient& client, TuiOptions options);    // TuiOptions created here in include/url_shortener/tui/tui_options.hpp (fields: theme, read_only, assume_yes, max_inline_bytes); subtask 19 maps CLI flags onto it
    int run();                                       // returns 0; builds frame, loads system.info, runs Loop
    void register_screen(std::unique_ptr<IScreen>);
};
```

D11 edge table (encode exactly): dashboard->{links,tables,import_export,jobs,palette}; links->{link_detail,link_form}; link_detail->link_form; link_form->confirm; confirm->{links,dashboard} (return target recorded by the pusher); tables->{row_json,link_form}; import_export->jobs; palette->generic_form; generic_form->confirm; jobs->job_detail. Tab keys are legal from any root screen (D1 says tabs are global).

## Implementation steps

1. Implement `Router` + `is_legal` from the table above; keep `confirm` a modal overlay, not a stack entry that changes the tab highlight (record `return_to`).
2. `FrameModel`: derive title from `SessionInfo` and `system.info` (`backend.metadata`, redacted DSN already by server; show the label only) - never display config values client-side.
3. `frame_view.cpp`: `window(text(title) , vbox({tabbar, separator(), body | flex, separator(), footer}))`; right-aligned `[F1 Help]` via `hbox({text(title), filler(), text("[F1 Help]")})`; banner as a one-line `inverted` row under the tab bar when present.
4. `App::run`: create `ScreenInteractive::Fullscreen()`, construct `FtxuiPoster`, `AsyncCaller`, call `system.info` asynchronously before first paint (show "connecting..." until the response; failure -> toast, frame still usable), root component = `CatchEvent(Container::Tab(screens, &active_index) wrapped in frame Renderer, global_key_handler)`.
5. Global key handler (only when no text `Input` has focus - see Constraints): digits and `:` call `navigate_tab`, `q`/Esc `pop`, `F1` toggles help (stub hook for 05), Ctrl-C exits via `screen.ExitLoopClosure()`.
6. Analytics tab ([6] in D1): register `placeholder_screen` rendering "Analytics: provided by Milestone 0002" (ADR 0003 non-goal); keep the tab visible to match D1.
7. 80x24 minimum: if `Terminal::Size()` is smaller render a single "terminal too small (need 80x24)" element instead of the frame.

## Commands

```powershell
cmake --build cmake-build --target tui__04_router_test tui__04_frame_model_test tui__04_frame_render_test
ctest --test-dir cmake-build -R "^tui__04_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `04_router_test.cpp` | `tui__04_router_test` | unit | start = dashboard; every legal D11 edge accepted, every other pair rejected (exhaustive 13x13 loop vs the table); `pop` at tab root is a no-op; tab keys from nested screens reset the stack; `confirm` return target restored |
| `04_frame_model_test.cpp` | `tui__04_frame_model_test` | unit | title for Admin vs User (`user: local/User`); banner only when `!persistent`; tab list has 6 entries + palette, active flag follows `ScreenId`; long backend label truncated at 60 chars with `...` |
| `04_frame_render_test.cpp` | `tui__04_frame_render_test` | unit | golden `d01_frame_80x24.txt` (placeholder body), `d01_frame_memory_banner_80x24.txt`, `d01_frame_narrow_60x20.txt` shows the too-small message; resizing 80x24 -> 100x30 keeps footer on last row |

Manual: start/quit cleanly in Windows Terminal, legacy conhost, a Linux terminal (see [20](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/20-manual-smoke-checklist.md)).

## Constraints

- Screens only get `ScreenContext` (client facade = `AsyncCaller` + `SessionInfo`); no registry/service access (C8).
- Global hotkeys must not fire while an `Input` has focus (typing `1` in a slug field must not switch tab): `ScreenContext::text_input_active` flag set by form/filter components.
- Layout per D1 verbatim at 80 columns; deviations amend ADR 0003.
- No `HttpServer`, no sockets, no `uri.txt` (C5).

## Success criteria

1. `d01_*` goldens match; D1 rows (title, tab bar, footer) appear in the described order.
2. The router edge table equals the D11 mermaid diagram edge-for-edge (reviewed against ADR 0003).
3. The app starts with only the placeholder screens, shows the frame, and exits on Ctrl-C with terminal state restored.
4. Typing digits into a focused `Input` does not change the tab.

## Out of scope / risks

- Real screen bodies (11-17), help overlay and toasts (05, 06).
- **ADR 0003 inconsistency:** D1 lists `[6]Analytics` but D11 and the Non-goals have no Analytics screen; this subtask renders a placeholder tab.
- Risk: FTXUI Ctrl-C handling differs per version; verify the loop always restores the terminal (alt screen, cursor) on exit paths including exceptions.
