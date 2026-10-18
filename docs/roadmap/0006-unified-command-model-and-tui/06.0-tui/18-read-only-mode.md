# 18 - Read-only mode and mutation safety

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02, 04, 06, 08, 11, 12, 13, 14, 15, 16, 17; 01.0/07 (registry `AccessGuard` enforcement)
**Blocks:** 06.0/20 (the CLI wiring only passes the `--read-only` flag through; the end-to-end read-only check runs in 20 and Task 07.0)
**Estimated size:** S
**Suggested agent:** cpp-expert, testing-expert

## Objective

Implement ADR 0003 rule 5: `url_shortener tui --read-only` runs the session as
`ControlSet::User`; write screens and actions render **disabled with the
registry's `not_authorized` reason, not hidden**. Centralises the capability
decision (`CapabilityModel`), shows the session role in the **D1** title
(`user: local/User`) with a `READ-ONLY` badge, and adds the cross-cutting safety
tests: no mutating request is ever sent from a read-only session, and confirm
policy cannot be bypassed. Covers the "disabled" variants of D3, D5, D6, D7,
D8, D9.

## Dependencies

- Internal: 06.0/02 (`SessionInfo.read_only`, `InProcessClient` built with `CallerContext{ControlSet::User}`), 04 (title), 06 (`ConfirmPolicy`), 08 (`run_mutation`, form Create button), 11 (palette disabled entries), 12-16 (per-screen action availability).
- `security::ControlSet` in `include/url_shortener/security/control_set.hpp` and `AccessGuard` (`requireRead/requireWrite`, verified) - the registry enforces; the TUI only mirrors for presentation.
- `system.info.read_only` (catalog 3.5) as the cross-check of the session.
- `link.export` has base permission `read`, but its `permission_for` hook (ADR 0002 section 2, decision log D16; Task 05.0 subtask 10)
  raises it to `write` when `path` is given, and the registry enforces that. `command.list` reports only the base `read`, so the TUI
  mirrors the rule explicitly for presentation: in read-only mode the Export sub-tab offers inline export only and shows the file-path
  field disabled with the `not_authorized` reason. If the TUI sent `path` anyway, the registry would answer `not_authorized`.
- External FTXUI: `dim`, `strikethrough`-free styling via `Theme::disabled`; no new components.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/capability.hpp` / `src/tui/capability.cpp` | create | `CapabilityModel` (model lib) |
| `src/tui/frame_view.cpp` | modify | role + `READ-ONLY` badge in the title (D1) |
| `src/tui/form_view.cpp`, `src/tui/screens/*.cpp` | modify | take `CapabilityModel` for disabled controls/hints (small edits listed per screen in their own subtasks; this subtask verifies) |
| `tests/unit/tui/18_capability_test.cpp`, `18_readonly_matrix_test.cpp`, `18_readonly_render_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d01_frame_readonly_80x24.txt`, `d09_palette_readonly_80x24.txt` (shared with 11), `d05_new_link_readonly_80x24.txt`, `d07_import_readonly_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct Capability { bool allowed; std::string reason; };         // reason == "not_authorized: read-only session" when !allowed
class CapabilityModel {                                           // NEW
public:
    explicit CapabilityModel(SessionInfo session);
    Capability can_run(const CommandInfo& command) const;         // read -> allowed; write|migrate|manage_users when read_only -> denied
    Capability can_run(const std::string& command_name, const std::vector<CommandInfo>& catalog) const;   // unknown command -> allowed=false, reason "unknown_command"
    bool read_only() const;
    std::string badge() const;                                    // "READ-ONLY" or ""
};
```

Reason text reuses the registry's code and wording style (`not_authorized`) so
that what is shown before a call and what the server would answer agree. If
the user nevertheless triggers a denied action through a stale UI path, the
server's `not_authorized` envelope is displayed by the normal toast (06).

## Implementation steps

1. Decide read-only at one place: `TuiOptions.read_only` (flag) builds `InProcessClient` with `ControlSet::User` (subtask 19); `SessionInfo.read_only` derived from the `CallerContext`; cross-check against `system.info.read_only` on start-up (mismatch -> toast and force read-only).
2. `CapabilityModel::can_run` is the only function screens call to enable/disable actions and palette entries; replace any ad-hoc `if (read_only)` introduced earlier (grep in test).
3. Disabled rendering rule: control shown dim with suffix `(read-only)`; activating shows a toast with the reason and sends nothing; footer hints for disabled actions are dim.
4. Title: `... -- user: local/User [READ-ONLY]` (D1 shows `user: local/Admin` today).
5. Cross-screen matrix test drives every screen with a Mock client and a read-only session and asserts `requests` contains only read commands.

## Commands

```powershell
cmake --build cmake-build --target tui__18_capability_test tui__18_readonly_matrix_test tui__18_readonly_render_test
ctest --test-dir cmake-build -R "^tui__18_" --output-on-failure
# end-to-end (needs 03.0+05.0 registry): in-process registry with ControlSet::User, no mock
ctest --test-dir cmake-build -R "^tui__18_readonly_matrix" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `18_capability_test.cpp` | `tui__18_capability_test` | unit | matrix permission (read/write/migrate/manage_users) x read_only on/off; unknown command; reason strings stable; badge text |
| `18_readonly_matrix_test.cpp` | `tui__18_readonly_matrix_test` | unit | for each screen (Links, LinkDetail, Tables, ImportExport, Jobs, Dashboard, Palette, GenericForm) feed a scripted key sequence that tries every action key; assert Mock saw zero write/migrate requests and reads still work; plus a registry-backed variant using `InProcessClient` + `ControlSet::User` against a test registry where a write attempt yields `not_authorized` and the toast shows it |
| `18_readonly_render_test.cpp` | `tui__18_readonly_render_test` | unit | goldens: frame badge, disabled Create button with reason, disabled import Start, palette dimming |

## Constraints

- C7: authorization lives in the registry; the TUI mirror is presentation only and must never be the sole guard (server check test above).
- Disabled, not hidden (ADR 0003 rule 5).
- `--yes` has no effect on permission (a read-only `--yes` session still denies writes).
- No secrets/IDs in reasons.

## Success criteria

1. `d01_frame_readonly_80x24.txt` shows `user: local/User` and the badge.
2. Across all screens, a read-only session emits zero `write`/`migrate` requests (matrix test) and the registry-backed variant proves server-side denial.
3. Every denied control shows the reason text (golden).
4. Searching `src/tui/` for `read_only` finds only `CapabilityModel`, `SessionInfo` and wiring (test).

## Out of scope / risks

- Per-user roles, login prompts (decision-log Q6: local operator = Admin), `manage_users` flows (Milestone 0002).
- Risk: if a future descriptor lacks an accurate `permission`, the UI would mis-enable it; the server still denies. Palette tests iterate the real catalog to catch missing permissions.
- Safety trade-off from 06: `--yes` also skips the typed-slug confirmation for `link.delete`; call out in the TUI guide.
