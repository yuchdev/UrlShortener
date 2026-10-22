# 21 - Settings screen (D13)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/02 (`ICommandClient`), 03 (`AsyncCaller`), 04 (frame/router - adds nav slot `[7]`), 05 (theme); 03.0/17 (`system.info`, extended with the `shortener` block)
**Blocks:** 06.0/20
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Render **D13**: a single, dedicated, read-only screen showing the server's
effective identity (version, backend names, persistence, current session's
read-only flag) and shortener configuration (base domain, generated slug
length, default redirect type, default expiry, `allow_private_targets`,
`analytics_enabled`, and the three request-hardening limits) - the full
`system.info` payload, laid out for a human instead of as a one-line status
summary. Added 2026-10-22, after the headless design pass, at the user's
request alongside the pre-existing Command Palette (11) and Tables browser /
"DB viewer" (14), so that all three screens the user asked for exist in this
milestone.

This screen is the **only** consumer of `system.info`'s new `shortener` block;
the Dashboard (17) keeps reading just `backend`/`persistent` as it always did
(no change to 17 from this subtask).

## Dependencies

- Internal: 06.0/02 (`ICommandClient::call`), 03 (`AsyncCaller`, one-shot - no
  polling: configuration does not change at runtime, so there is no 5 s
  ticker like the Dashboard's), 04 (adds the `[7]Settings` nav slot and the
  `7` hotkey to the router - a router-table change, not a new router type),
  05 (theme/mono rendering).
- Commands used: `system.info` only, called once on `on_enter` and again on
  manual refresh (`r`). No other command is ever issued by this screen.
- `system.info`'s result shape (post-03.0/17 extension, verified against that
  subtask's updated spec): `{version, backend:{metadata,cache,analytics,
  rate_limit}, persistent, read_only, shortener:{base_domain,
  generated_slug_length, default_redirect_type, default_expiry_seconds,
  allow_private_targets, analytics_enabled, max_request_body_bytes,
  max_request_target_length, request_id_max_length}}`.
- External FTXUI: `window`, `hbox`, `vbox`, `text`, `separator` - no `Table`,
  no `Input`, no `Button` (nothing on this screen is editable or focusable
  beyond `r`/`q`, consistent with ADR 0003's Non-goals: there is no
  `settings.set` command for this screen to call).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/settings_model.hpp` / `src/tui/settings_model.cpp` | create | `SettingsModel`: one `PanelState` (model lib), reusing the `PanelState` enum already defined by 06.0/17 (do not redefine it) |
| `src/tui/screens/settings_screen.cpp` | create | `SettingsScreen : IScreen` (UI lib) |
| `tests/unit/tui/21_settings_model_test.cpp`, `21_settings_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d13_settings_80x24.txt`, `d13_settings_unavailable_80x24.txt`, `d13_settings_readonly_80x24.txt`, `d13_settings_mono_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register the two new test files under `TUI_MODEL_UNIT_SOURCES`/`TUI_RENDER_UNIT_SOURCES` |

## Classes and functions

```cpp
// namespace url_shortener::tui (model lib - no FTXUI type anywhere in this header)
class SettingsModel {                                              // NEW
public:
    commands::CommandRequest refresh_request(const std::function<std::string()>& next_id) const;  // one request: system.info, no args
    void apply(const commands::CommandResponse& r);                 // ok -> ready; error/exception -> unavailable
    PanelState state() const;
    const app::SystemInfo& info() const;   // valid only when state() == PanelState::ready; default-constructed otherwise
    bool read_only() const;                // from the response's read_only field, not from the session's own --read-only flag
};
```

`SettingsModel` reuses `app::SystemInfo`/`ShortenerSettings` (03.0/17) as its
view type via `json_view` deserialization (K7) - it does not redeclare the
fields, so a field added to `ShortenerSettings` later needs no change here
beyond the renderer picking it up.

## Implementation steps

1. `SettingsScreen::on_enter` submits `refresh_request()` through `AsyncCaller`; `r` resubmits; no other trigger (no timer, unlike 17).
2. Render two columns per D13: "Backend" (metadata/cache/analytics/rate limit names, `persistent`) and "Shortener" (the nine `shortener` fields); a third block "Request hardening" (the three limit fields); `default_expiry_seconds` renders `never` when the JSON value is `null`, else `"<N> s"`.
3. Header line: `Version <version>` and `Read-only session <yes|no>` from the response's own `read_only` (not the TUI process's `--read-only` flag directly - they are expected to agree, but the screen shows what the server reported, matching D4/D2's existing pattern of trusting the command result over local state).
4. Footer line (per D13's sketch): a fixed, non-interactive reminder that this view is read-only and how settings are actually changed (CLI flags/env + restart) - plain `text()`, not a form.
5. One failing `system.info` call -> whole screen shows `PanelState::unavailable` with the error's `message`/`request_id` (there is only one panel here, unlike Dashboard's six independent ones, so "degrade only its panel" from 17 collapses to "degrade the one panel there is").

## Commands

```powershell
cmake --build cmake-build --target tui__21_settings_model_test tui__21_settings_screen_test
ctest --test-dir cmake-build -R "^tui__21_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `21_settings_model_test.cpp` | `tui__21_settings_model_test` | unit | `apply` with a full success response populates every `SystemInfo`/`ShortenerSettings` field exactly; `default_expiry_seconds` null vs. a number; error response -> `state() == unavailable`, `info()` stays default; `read_only()` reflects the response's `read_only`, independent of any local flag; `refresh_request()` carries no args and `permission` metadata marks it `read` (asserted against the catalog, not hand-typed) |
| `21_settings_screen_test.cpp` | `tui__21_settings_screen_test` | unit | goldens: complete (`d13_settings_80x24`), unavailable (`d13_settings_unavailable_80x24`), read-only session (`d13_settings_readonly_80x24`, confirms the header reads "Read-only session yes" - this is a rendering difference, not a disabled control, so it does **not** go through 06.0/18's disable-with-reason machinery), mono (`d13_settings_mono_80x24`); `r` triggers exactly one new `system.info` call; no other command is ever issued by this screen (assert the full set of requests sent over the test) |

## Constraints

- Only `system.info`; no mutating command is reachable from this screen, and there must be no path to one (C8's include-boundary test already forbids a DTO/service header here; this subtask additionally asserts the request set at the command level, test above).
- Never renders `tls`, `analytics_client_hash_salt`, or any field `system.info` does not emit - this screen renders exactly the deserialized struct, so once 03.0/17's own secret test passes, there is nothing left for this screen to leak; it adds no new secret-handling logic of its own.
- No raw client identifiers (C6 - not applicable here since this screen has none, verified by the field list above).
- C8: JSON via `json_view` (over the 01.0/01 scanner), same as every other screen.
- This screen is always read-only, for every role including `ControlSet::Admin` - do not route it through 06.0/18's write-control-disabling logic; there is no write control to disable.

## Success criteria

1. `d13_settings_80x24.txt` shows every field named in the Objective, laid out per D13 (ADR 0003), at 80x24.
2. A failing `system.info` call renders the single `unavailable` state with a visible `message`/`request_id`, never a blank or crashed screen.
3. The full set of commands this screen ever issues is exactly `{system.info}` (asserted, not just manually inspected).
4. `7` from the Dashboard (and the D11 state-machine edge `Dashboard --> Settings: 7`) opens this screen; `q` returns to Dashboard.

## Out of scope / risks

- Editing any setting (ADR 0003 Non-goals: no `settings.set` command exists or is planned in this milestone).
- Live-reload on `SIGHUP` or any other out-of-band config change: this screen only ever reflects the single startup snapshot `system.info` already serves (`app::process_system_info()`, set once at bootstrap per 03.0/17 - a `SIGHUP` reload, if `main.cpp` ever adds one for TLS, is out of scope for this subtask and not currently wired to update that snapshot).
- Network/TLS/listener settings (ports, HSTS, cert paths): deliberately excluded from `system.info` itself (03.0/17), not merely hidden here - there is nothing for this screen to show even if it wanted to.
