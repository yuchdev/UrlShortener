# 08 - Form widgets and generic form screen (D5)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 05, 06, 07
**Blocks:** 11, 12, 13, 14, 15, 18, 20
**Estimated size:** L
**Suggested agent:** cpp-expert

## Objective

Render a `FormModel` as FTXUI components: one widget family per `ArgKind`
(text `Input`, `Checkbox`, `Radiobox`/`Dropdown`, composite list/map/object
rows) and a `FormScreen` that frames them as **D5**: title
`New link  (command: link.create)`, label column with `*` for required,
hint column ("blank = generated", "ISO-8601, blank = never"), `[ Create ]  [ Cancel ]`
buttons, the live validation line, and the `Equivalent:` footer. The form is the
Generic form of **D11** (palette -> GenericForm -> Confirm) and is reused by
the Links, Tables and Import screens.

## Dependencies

- Internal: 06.0/07 (`FormModel`), 06.0/06 (`ConfirmPolicy`, `equivalent_*`, `toast_from_response`), 03 (`AsyncCaller`), 04 (`ScreenContext.text_input_active`), 05 (`Theme`).
- External FTXUI: `Input(StringRef content, StringRef placeholder, InputOption)` with `InputOption::multiline=false` and `on_change` (**VERIFY AT PIN TIME**), `Checkbox(ConstStringRef label, bool* checked)`, `Radiobox(ConstStringRefVector entries, int* selected)` with `RadioboxOption::direction` for horizontal layout (VERIFY), `Dropdown(ConstStringRefVector, int* selected)`, `Button(ConstStringRef label, std::function<void()>)`, `Container::Vertical(children, int* selected)`, `Container::Horizontal`, `Renderer(Component, std::function<Element()>)`, `hbox`, `vbox`, `size(WIDTH, EQUAL, n)`, `dim`, `separator`, `window`, `yframe` for scrolling when the form exceeds the terminal height.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/form_view.hpp` | create | `make_form_component(FormModel&, FormViewContext&)`, `FormViewContext` |
| `src/tui/form_view.cpp` | create | composition of rows and buttons |
| `src/tui/form_widgets.cpp` | create | `make_field_widget(FormField&, const Theme&)` per `ArgKind` |
| `include/url_shortener/tui/form_screen.hpp` / `src/tui/screens/form_screen.cpp` | create | `FormScreen : IScreen` (id `generic_form` or `link_form`, set at construction) |
| `src/tui/mutation_flow.cpp` + header | create | `run_mutation(request, info, ctx, on_result)`: ConfirmPolicy -> confirm dialog -> `AsyncCaller::submit` -> toast; **single choke point** for mutating submits (used by 11-16) |
| `tests/unit/tui/08_form_widgets_render_test.cpp`, `08_form_screen_test.cpp`, `08_mutation_flow_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d05_new_link_80x24.txt`, `d05_new_link_errors_80x24.txt`, `d05_edit_link_80x24.txt`, `d05_generic_enum_dropdown_80x24.txt`, `d05_form_scroll_80x12.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct FormViewContext {                                // NEW
    AsyncCaller& caller;  ConfirmPolicy& policy;  ToastQueue& toasts;  const Theme& theme;
    bool read_only;                                     // from 18; disables submit when command is not read
    std::function<void(const commands::CommandResponse&)> on_success;   // screen decides what to refresh/navigate
    std::function<void()> on_cancel;
};
ftxui::Component make_form_component(FormModel& model, FormViewContext& ctx);       // D5 body + buttons + footer
ftxui::Component make_field_widget(FormField& field, const Theme& theme);            // per-kind
void run_mutation(commands::CommandRequest request, const CommandInfo& info, FormViewContext& ctx);
```

Widget mapping (the "one widget family per ArgSpec type" table):

| ArgKind | FTXUI | Notes |
|---|---|---|
| string / integer / timestamp | `Input`, single line | integer: reject non-digit keystrokes via `on_change` revert; timestamp placeholder `YYYY-MM-DDThh:mm:ssZ` |
| boolean | `Checkbox` | D5 `enabled [x]`; tri-state not shown; untouched until toggled |
| enumeration, <= 3 values | `Radiobox` horizontal | D5 `redirect ( ) permanent (*) temporary` |
| enumeration, > 3 values | `Dropdown` | e.g. `link.list.sort` |
| string-list | `Input` + hint "comma separated" | D5 tags |
| string-map | `Input` + hint "key=value, comma separated" | D5 metadata |
| object (with `members`) | `Container::Horizontal` of N `Input`s with placeholders (`[name] [source] [medium]`); remaining members on a second row | D5 campaign |
| object (no members) | `Input` raw JSON | fallback |
| nullable fields | extra `[x] clear` Checkbox bound to `clear_field` | `link.update` |

## Implementation steps

1. Layout helper: label column width = max label length (cap 14), hint column dim; rows are `hbox({text(label) | size(WIDTH, EQUAL, w), widget | flex, hint})`.
2. Wire `Input::on_change` to `FormModel::set_text` and recompute `validate()` lazily on blur/Enter, not each keystroke, to keep 80x24 redraw cheap.
3. Buttons: Create (label from descriptor verb: `Create`, `Save`, `Run`), Cancel. Create is disabled and dim when `ctx.read_only && command.permission != "read"` (shows the reason text from 18) or while `caller.busy()`.
4. Submit path: `FormModel::validate` -> `to_request(RequestIdGenerator::next())` -> `run_mutation` for mutating commands or direct `AsyncCaller::submit` for read commands -> on `ok:false` toast + inline `FormField::error` for `invalid_field` (server message names the field when it can; otherwise toast only); on `ok:true` call `ctx.on_success`, toast success; for job responses (`job` key) the toast says "job j-9 accepted" with `[5] Jobs` hint.
5. Footer `Equivalent:` = `equivalent_cli(model.to_request(...))`, refreshed after each change; truncated with `...` to width, full text available via `y` (copy-hint toast) - clipboard is out of scope, so `y` just shows the full line in a toast.
6. Keyboard: Tab / Shift-Tab and arrows move focus (FTXUI `Container` default), Enter on a button activates, Ctrl-S submits from anywhere, Esc cancels (with discard prompt only if any field is touched).
7. Set `ScreenContext.text_input_active` while an `Input` has focus so global hotkeys stay quiet.

## Commands

```powershell
cmake --build cmake-build --target tui__08_form_widgets_render_test tui__08_form_screen_test tui__08_mutation_flow_test
ctest --test-dir cmake-build -R "^tui__08_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `08_form_widgets_render_test.cpp` | `tui__08_form_widgets_render_test` | unit | render each `ArgKind` fixture to text; D5 golden (`link.create`) at 80x24; required `*`; error state golden; Dropdown golden for 5-value enum; form taller than screen scrolls and keeps focused row visible (80x12 golden); mono theme variant is ASCII |
| `08_form_screen_test.cpp` | `tui__08_form_screen_test` | unit | with `MockCommandClient` and `InlinePoster`: type target URL via events, Ctrl-S -> exactly one `link.create` request with golden args; server `invalid_url` -> toast `! invalid_url`, form stays open with data preserved; success -> `on_success` once; double Ctrl-S while busy sends one request; Esc with touched fields prompts, untouched cancels immediately; read-only context disables Create and sends nothing |
| `08_mutation_flow_test.cpp` | `tui__08_mutation_flow_test` | unit | mutating command without `--yes` shows confirm before any `call` (mock saw 0 requests until confirmed); `link.delete` needs token; `--yes` skips confirm; cancel -> 0 requests; non-mutating command bypasses confirm |

Manual: D5 appearance in Windows Terminal and legacy conhost; paste of a long URL into the target field.

## Constraints

- Screens never build JSON; only `FormModel::to_request` does (C1, C8).
- All mutating submits go through `run_mutation` (ADR 0003 rule 4).
- `Equivalent:` footer must be byte-identical to the request actually sent (test asserts equality with the mock's recorded request).
- No blocking calls on the UI thread (rule 3).
- 80x24 minimum layout, mono theme supported.

## Success criteria

1. `d05_*` goldens match; D5 rows (title, fields, buttons, validation line, Equivalent footer) present.
2. Any registry command added later renders and submits through `FormScreen` with no TUI code change (test with a synthetic `CommandInfo` containing every `ArgKind`).
3. The recorded request equals the footer text and the golden JSON.
4. Zero requests reach the mock for cancelled/denied mutations.

## Out of scope / risks

- Multi-line text areas beyond the import paste box (15 builds its own), clipboard integration, wizard flows.
- Risk: FTXUI `Input` multiline default and cursor handling vary by version; covered by VERIFY markers and the golden tests.
- Risk: radiobox horizontal layout option may not exist; fallback is a custom `Renderer` over `Menu` with `MenuOption::Horizontal()`.
