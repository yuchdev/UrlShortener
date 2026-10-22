# 06 - Toast and confirm components (D10)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02, 03, 04, 05
**Blocks:** 07, 08, 11, 12, 13, 15, 16, 17, 18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement the two shared feedback components of **D10**: the error/info
**toast** (`+- ! slug_conflict -+`, always `error.code` + `message`, `[d]etails`
shows the raw envelope) and the **confirm dialog** (`Soft-delete "promo"? Type
the slug to confirm: [promo]`), plus the confirm *policy* that decides when a
mutation needs which kind of confirmation (ADR 0003 rule 4). Also provides the
`Equivalent:` command-line renderer reused by the form footer of D5.

## Dependencies

- Internal: 06.0/02 (`CommandInfo`, `json_view::pretty_print`), 06.0/03 (`AsyncCaller`), 06.0/04 (`ScreenContext`, `Router`), 06.0/05 (`Theme`). `core/clock.hpp` `ManualClock` for deterministic toast expiry (verified to exist; exact interface **UNVERIFIED**).
- External: FTXUI `Modal`-style composition (`dbox`, `clear_under`, `center`, `window`), `Input(StringRef, StringRef, InputOption)`, `Button(label, std::function<void()>)`, `Container::Horizontal`, `CatchEvent`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/toast.hpp` / `src/tui/toast.cpp` | create | `Toast`, `ToastQueue`, `toast_from_response` (model lib) |
| `include/url_shortener/tui/confirm.hpp` / `src/tui/confirm.cpp` | create | `ConfirmModel`, `ConfirmPolicy` (model lib) |
| `include/url_shortener/tui/equivalent_cmd.hpp` / `src/tui/equivalent_cmd.cpp` | create | `equivalent_cli(request)`, `equivalent_envelope(request)` (model lib) |
| `src/tui/toast_view.cpp`, `src/tui/confirm_view.cpp` | create | FTXUI components (UI lib) |
| `tests/unit/tui/06_toast_test.cpp`, `06_confirm_test.cpp`, `06_equivalent_cmd_test.cpp`, `06_dialog_render_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d10_confirm_delete_80x24.txt`, `d10_toast_error_80x24.txt`, `d10_toast_details_80x24.txt`, `d10_confirm_yesno_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
enum class ToastKind { info, success, error };
struct Toast { ToastKind kind; std::string title; std::string body; std::string source_command;
               std::string request_id; std::string raw_envelope; std::chrono::steady_clock::time_point expires; };

class ToastQueue {                                     // NEW
public:
    explicit ToastQueue(const core::Clock& clock, std::chrono::seconds ttl = std::chrono::seconds(6)); // Clock type UNVERIFIED
    void push(Toast t);                                // max 3 visible; oldest dropped; error toasts never auto-expire until dismissed
    void tick();                                       // expire info/success
    const std::deque<Toast>& visible() const;
    void dismiss_front();
};
Toast toast_from_response(const std::string& command, const std::string& request_id, const commands::CommandResponse& r);
//  ok -> success "command ok"; error -> kind=error, title="! <error.code>", body=<error.message>, raw_envelope kept for [d]etails

enum class ConfirmKind { none, yes_no, type_token };
struct ConfirmRequest { ConfirmKind kind; std::string title, prompt, token; };
class ConfirmPolicy {                                  // NEW
public:
    explicit ConfirmPolicy(bool assume_yes);           // --yes
    ConfirmRequest decide(const CommandInfo& info, const std::string& args_json) const;
    // not mutating -> none; mutating -> yes_no; link.delete -> type_token(slug);
    // any descriptor with permission "migrate" -> type_token("MIGRATE") (reserved rule, ADR 0003 rule 4: no such command exists
    // in Milestone 0006, so the branch is inert and covered by a unit test with a fake CommandInfo); assume_yes -> none
};
class ConfirmModel {
public:
    explicit ConfirmModel(ConfirmRequest req);
    void set_input(std::string s);
    bool can_submit() const;                           // yes_no: true; type_token: input == token (exact, case-sensitive)
    void submit();  void cancel();
    std::optional<bool> result() const;                // nullopt until decided
};
std::string equivalent_cli(const commands::CommandRequest& r);        // url_shortener cmd link.create --json '{"target_url":"..."}'
std::string equivalent_envelope(const commands::CommandRequest& r);   // exact request envelope text
// UI lib
ftxui::Component toast_overlay(ToastQueue& queue, const Theme& theme);                 // [d]etails toggles raw view, Esc dismisses
ftxui::Component confirm_dialog(ConfirmModel& model, const Theme& theme);              // Enter submits when allowed, Esc cancels
```

## Implementation steps

1. Implement `toast_from_response` from `ok`/`error.code`/`error.message` via `json_view` (messages longer than 200 chars truncated with `...` for display; raw kept intact).
2. `ToastQueue` uses the injected clock; render position: bottom-right over the body, never over the footer.
3. `ConfirmPolicy::decide` extracts `slug` for `link.delete` with `get_string(args_json, "slug")`; missing slug -> falls back to `yes_no` (the registry will reject the call anyway).
4. `equivalent_cli` must shell-quote the JSON for the platform shown (single quotes as in D5/catalog; on Windows PowerShell docs note double-quote escaping is the user's job); never include secrets (none exist in args).
5. Confirm dialog: `Input` for the token (single-line; set `InputOption::multiline = false` - **VERIFY AT PIN TIME** that the option exists, since newer FTXUI makes `Input` multiline by default); `[ Delete ]` button disabled (dim, ignores Enter) until `can_submit()`.
6. Screens call `ConfirmPolicy::decide`; if `kind == none` they submit directly. Confirm result continuation is a `std::function<void(bool)>`, executed on the UI thread, which then calls `AsyncCaller::submit`.

## Commands

```powershell
cmake --build cmake-build --target tui__06_toast_test tui__06_confirm_test tui__06_equivalent_cmd_test tui__06_dialog_render_test
ctest --test-dir cmake-build -R "^tui__06_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `06_toast_test.cpp` | `tui__06_toast_test` | unit | error envelope -> title `! slug_conflict`, body, raw kept; success auto-expires after ttl (ManualClock +6 s); error toast persists until dismissed; 4th toast evicts the oldest; malformed response (no `ok`) -> generic `! internal` toast, no crash |
| `06_confirm_test.cpp` | `tui__06_confirm_test` | unit | policy matrix: non-mutating -> none; `link.create` -> yes_no; `link.delete` -> type_token(slug); `assume_yes` -> none for all; token exact match (case, trailing space rejected, empty rejected); cancel -> `result()==false`; slug with unicode/quotes handled |
| `06_equivalent_cmd_test.cpp` | `tui__06_equivalent_cmd_test` | unit | CLI line for `link.create` equals catalog example form; args with quotes/backslashes escaped; envelope text contains `"v":1`; empty args -> `--json '{}'` |
| `06_dialog_render_test.cpp` | `tui__06_dialog_render_test` | unit | goldens for D10 left box (Delete disabled then enabled after typing `promo`), right toast, details view; keys: Esc cancels, Enter ignored until token matches |

## Constraints

- ADR 0003 rule 4: every `mutating = true` command confirms unless `--yes`; `link.delete` requires typing the slug.
- Errors always show `error.code` + `message` from the envelope, never TUI-invented text (D10 note).
- Typed-token confirmation is skipped by `--yes` (ADR wording "skippable with --yes"); document in risks.
- No secret data in toasts or `equivalent_*` output (C6 spirit).

## Success criteria

1. D10 goldens match in both halves (confirm box, error toast).
2. A mutating call cannot reach `AsyncCaller::submit` from screens without passing `ConfirmPolicy` (enforced by routing all mutation submits through one helper added in subtask 08; test there).
3. `[d]etails` shows the pretty-printed raw envelope identical to what the client returned.
4. Toast expiry is deterministic under `ManualClock`.

## Out of scope / risks

- Audio/bell, multi-line toasts, toast history (a history view could use `ToastQueue` later).
- Risk: `--yes` removing typed confirmation for `link.delete` is a safety trade-off requested by ADR 0003 wording; flag for review in [18](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/18-read-only-mode.md) (read-only sessions never reach this point).
