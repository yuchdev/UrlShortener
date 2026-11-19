# QA Manual Smoke Checklist - Terminal UI (TUI)

**Feature:** interactive `url_shortener tui` mode (Milestone 0006, Task 06.0)
**Stage:** TUI transport adapter (`src/tui/`) over the shared command registry
**Owner:** testing-expert / docs-writer
**Format convention:** mirrors
[`cli_link_commands.md`](./cli_link_commands.md) - numbered cases, exact
commands per OS, an expected block, and a results table.

This checklist covers what the headless FTXUI snapshot suite
(`ctest -L unit -R "^tui__"`, goldens under `tests/unit/tui/golden/`) **cannot**
prove: real terminals, live key handling, resize, termios restoration,
persistence across restarts, the TUI/server separation contract (C5: no
listener, no `uri.txt`), and that each diagram **D1-D13** of
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md) renders as
specified (or the ADR is amended). The automated gate must be green before a
manual pass is recorded.

---

## Environment used for the recorded run

| Item | Value |
|------|-------|
| Built binary | `cmake-build/url_shortener` |
| Build hash | first pass `6660172`; re-run after fixes `69114b5` then `57cbc39` (`git rev-parse --short HEAD`) |
| Host | macOS (Darwin), `TERM=xterm-256color` |
| Driver | Python `pty` harness (scratch-only, not in the repo): opens a real pty, sets the window size with `fcntl`/`TIOCSWINSZ`, streams keystrokes, and checks `stty -a` (echo/canonical) before and after exit |
| Storage | disposable SQLite config in a scratch dir (`metadata: sqlite`, `cache: inmemory`, `analytics: inmemory`); never production data |
| Seed | ~30 links via `url_shortener cmd link.create --storage-config <cfg>` (28 active, 1 disabled `promo`, 1 deleted `old-blog`, plus CJK-in-target `cjk-url` and CJK-in-metadata `cjk-meta`) |
| Storage flag | confirmed `--storage-config <FILE>` via `url_shortener tui --help` |

> **Non-ASCII tag note:** `link.create` rejects non-ASCII **tags**
> (`invalid_field: tags violate constraints`), so S20's CJK data is carried in
> the **target URL** and **metadata** instead, which are accepted.

### How to reproduce the harness run

Seed and launch (bash):

```bash
cfg=./storage.sqlite.yaml   # metadata: sqlite + cache/analytics: inmemory
for i in $(seq 1 28); do
  url_shortener cmd link.create --storage-config "$cfg" \
    --json "{\"target_url\":\"https://example.com/page/$i\",\"slug\":\"seed-$(printf %02d $i)\"}"
done
url_shortener tui --storage-config "$cfg"
```

PowerShell (Windows Terminal / conhost):

```powershell
$cfg = ".\storage.sqlite.yaml"
1..28 | ForEach-Object {
  $s = "seed-{0:d2}" -f $_
  .\cmake-build\url_shortener.exe cmd link.create --storage-config $cfg `
    --json "{""target_url"":""https://example.com/page/$_"",""slug"":""$s""}"
}
.\cmake-build\url_shortener.exe tui --storage-config $cfg
```

---

## Automated gate (run first, must be green)

```bash
cmake --build cmake-build
ctest --test-dir cmake-build -L unit -R "^tui__" --output-on-failure
```

Recorded: **56/56 passed** at `6660172` (first pass); **57/57 passed** at
`57cbc39` (after the fixes, which added a Ctrl-C-while-typing regression test).

---

## Results summary

The three bugs found in the first pass (BUG-1/2/3) were fixed in `69114b5`; a
fourth, a Ctrl-C-while-typing regression introduced by that fix (BUG-4), was
fixed in `57cbc39`. Results below are the final re-tested state.

| # | Area (diagram) | Result | Note |
|---|---|---|---|
| S1 | Start/quit (D1) | **PASS** (fixed `69114b5`) | Frame matches D1; Ctrl-C now exits `0`; SIGTERM restores echo/canonical. First pass FAILed (Ctrl-C exited 130 - BUG-1). |
| S1b | Ctrl-C while typing | **PASS** (fixed `57cbc39`) | Ctrl-C exits `0` + termios restored with the Links filter, a form field, and the Import paste focused. First seen after `69114b5`: filter **hung** (force-killed), form/paste exited 130 (BUG-4). |
| S2 | Min size (D1) | **PASS** | 79x23 -> "terminal too small (need 80x24)"; 80x24 and 140x40 reflow; live `SIGWINCH` resize reflows; no crash. |
| S3 | Navigation (D11) | **PASS** (fixed `69114b5`) | `1`-`7` and `:` switch tabs; `q`/Esc pops nested screens; `q` at a non-Dashboard tab root returns to Dashboard (fixed `47ac255`). First pass FAILed (nav dead - BUG-2). |
| S4 | Typing vs hotkeys | **PASS** | In the Links filter, typing `1 2 : q` yields `filter: [12:q]`; no navigation. |
| F1 | Help while typing | **PASS** | F1 opens the help overlay even while an Input is focused; `?` stays typable. |
| S5 | Dashboard (D2) | **PASS** | Counts match `link.list` totals (30 / 28 / 1 / 1); 4 count calls issued; labels visible; no freeze. |
| S6 | Links (D3) | **PASS** | Filter bar, status radio, `PgDn` -> `page 2/2 (20 per page)`, selection stays visible. (Links sort key is off by design - `s` = stats.) |
| S7 | Create/delete (D5,D10) | **PASS** | `n` opens the D5 create form with the `Equivalent:` footer; `d` opens a D10 confirm that requires typing the slug. |
| S8 | Detail/stats (D4) | **LIMITED** | Server redirect returns `302`, but the TUI (separate process) cannot observe another process's analytics - only `noop`/`inmemory` analytics backends exist, neither shared cross-process. Not a TUI defect. |
| S9 | Tables (D6) | **PASS** (unblocked `69114b5`) | `3` opens Tables showing only `links`/`click_events`/`jobs`; columns `id`/`slug`/`url`, no hash/token/secret columns. |
| S10 | Import/export (D7,D8) | **PASS** (unblocked `69114b5`) | `4` opens the D7 import form (source/format/on-conflict/paste, `Ctrl-S` starts). |
| S11 | Jobs (D8) | **PASS** | A ~5,000-row file-path import, cancelled mid-run via Jobs, reaches `state: cancelled` with partial effects (`done: 972`, 972 links created). (CPU-idle-when-done sub-check not measured.) |
| S12 | Palette (D9) | **PASS** (unblocked `69114b5`) | `:` opens the palette; typing `link.` lists the `link.*` catalog with permission labels. |
| S13 | Read-only | **PASS** | `[READ-ONLY]` badge; Dashboard `total`/`deleted`/redirects show `n/a (read-only)`; `active`/`disabled` still load. |
| S14 | Mono/ASCII | **PASS** | `--theme mono` renders pure ASCII, overlay included. |
| S15 | Slow backend | **NOT RUN** | Requires a slow/unavailable Postgres or a sleep-injecting test backend; none available on this host. |
| S16 | C5 - no listener | **PASS** | `lsof` shows no LISTEN socket for the TUI pid; no `uri.txt` in cwd (only the expected `url_shortener-tui.log`). |
| S17 | Persistence | **PASS** | Two SQLite restarts both read the persisted store (30 -> 30); a new write is visible after restart (-> 31). In-memory launch shows the `memory store - data is lost on exit` banner. |
| S18 | Non-TTY | **PASS** (fixed `69114b5`) | `tui < /dev/null` and `tui | cat` exit `2`; stdout empty; **exactly one** stderr line. First pass had an extra `job_runner_shutdown` INFO line (BUG-3). |
| S19 | OFF build | **NOT RUN** | Optional. Message/exit verified in source (`src/main.cpp:226-228`: "built without TUI support (URLSHORTENER_ENABLE_TUI=OFF)", exit `2`); a separate `cmake-build-notui` OFF build was not performed. |
| S20 | Unicode | **PASS** | CJK target `https://example.com/世界/café` renders in the table with the `STATUS`/`CLICKS` columns aligned to the ASCII row above; no multibyte truncation garbage. CJK **tags** are rejected by validation (expected). |
| S21 | Settings (D13) | **PASS** (unblocked `69114b5`) | `7` opens the D13 Settings screen; every field matches the startup config; no TLS path, port, or salt appears (a secret-scan of its `system.info` source is empty). |

**Carry-forward checks**

| Check | Result | Note |
|---|---|---|
| termios restored after Ctrl-C | **PASS** | `stty -a` echo/icanon restored (checked before/after each run), including with a text input focused (S1b). |
| termios restored after `kill -TERM` | **PASS** | echo/canonical restored; process exits via SIGTERM (`-15`) with a clean terminal. |
| TUI/server separation (C5) | **PASS** | S16; also the redirect server's listen socket is gone after stop. |

---

## Findings

All four bugs below were found by this manual smoke pass and fixed by
`cpp-expert`; each was re-tested green against the fix (see the result rows).

| Bug | Items | Found at | Fixed in | Re-test |
|---|---|---|---|---|
| BUG-1 | S1 | 6660172 | `69114b5` | Ctrl-C exits 0 |
| BUG-2 | S3, S9, S10, S12, S21 | 6660172 | `69114b5` | all tabs navigate; screens reachable |
| BUG-3 | S18 | 6660172 | `69114b5` | exactly one stderr line |
| BUG-4 | S1b | 69114b5 (regression) | `57cbc39` | Ctrl-C exits 0 while typing |

### BUG-1 (S1) - FIXED in `69114b5` - Ctrl-C exited via SIGINT (130), not 0

- **Repro:** launch `url_shortener tui`, press `Ctrl-C`. **Expected** (ADR D1 /
  S1): clean exit code `0`. **Actual:** process terminates via **SIGINT**
  (shell `130`, `waitpid` signal 2). The terminal **is** restored (alt-screen
  exited, cursor shown, echo/canonical back on).
- **Mechanism:** FTXUI v7.0.3 defaults `force_handle_ctrl_c_ = true`
  (`_deps/ftxui-src/.../app.cpp`), so on `Event::CtrlC` it always
  `RecordSignal(SIGINT)` and re-raises SIGINT at teardown - regardless of the
  app handler. `src/tui/app.cpp:320-323` handles `Event::CtrlC` with
  `screen.Exit()` intending a `return 0`, but that path cannot win while
  `force_handle_ctrl_c_` is true; the app never calls `ForceHandleCtrlC(false)`.
- **Fix (`69114b5`):** `ForceHandleCtrlC(false)` is set so the app's
  `Event::CtrlC -> screen.Exit()` path produces exit `0`. Re-tested: plain
  Ctrl-C exits `0`, termios restored.

### BUG-2 (S3, blocked S9/S10/S12/S21) - FIXED in `69114b5` - global tab navigation dead at launch

- **Repro:** launch `url_shortener tui --storage-config <cfg>`; press `2`
  (or `3`/`4`/`5`/`7` or `:`). **Expected:** switch tab (D11). **Actual:**
  nothing - the view stays on the Dashboard. `F1`/`?` (help) and `Ctrl-C`
  still work. Confirmed: after `2`/`7` no Links/Settings content ever renders.
- **Root cause:** every screen shares one `ScreenContext` (constructed once in
  `src/tui/app.cpp:157`, passed to all 7 screen factories). The Import/Export
  screen's constructor calls `reset_focus()`
  (`src/tui/screens/import_export_screen.cpp:147`) ->
  `update_text_input_active()` (`:298`, `:332-336`), and its default focus is
  the paste text area (`is_text(Ctl::paste)` is true, `:93-96`), so the
  **shared** `context.text_input_active` is set to `true` at startup. The App's
  global hotkey handler returns early when `text_input_active` is true
  (`src/tui/app.cpp:317`), so digit/`:`/`q` navigation is swallowed before it is
  ever evaluated. No screen resets the flag on the Dashboard, so it stays true.
- **Why snapshot tests miss it:** goldens render one screen over its own
  `ScreenContext`; the poisoning only occurs when all 7 screens are built over a
  single shared context in the real `App`.
- **Partial workaround observed:** the Dashboard's `Enter` calls
  `router.navigate_tab(...)` directly (`src/tui/screens/dashboard_screen.cpp:
  134-138`), bypassing the guard, so Links (and Jobs, by `j/k` selection) are
  still reachable; Tables/Import/Export/Settings/Palette are not.
- **Fix (`69114b5`):** `text_input_active` is now per-screen and reset on tab
  entry, so a constructed-but-inactive screen no longer poisons the active
  screen's navigation. Re-tested: `1`-`7` and `:` all switch tabs.

### BUG-3 (S18, minor) - FIXED in `69114b5` - extra INFO line on the non-TTY refusal

- On `tui < /dev/null`, stdout is empty and stderr carries the one-line refusal,
  but also one `ts=... level=INFO component=job_runner msg=job_runner_shutdown`
  line (the global job runtime logs to the default stderr sink because the
  early non-TTY refusal returns before logging is initialized to a File sink).
  stdout - the screen-corruption-risk stream - stays clean. Low severity.
- **Fix (`69114b5`):** the non-TTY refusal now happens before the job runtime
  is built, so no shutdown line is logged. Re-tested: exactly one stderr line.

### BUG-4 (S1b) - FIXED in `57cbc39` - Ctrl-C while typing did not exit cleanly (regression from `69114b5`)

- **Repro (against `69114b5`):** launch `url_shortener tui`; focus a text input;
  press `Ctrl-C`. **Expected:** exit `0`, terminal restored. **Actual:** depends
  on the Input:
  - Links filter (`2` then `/`, then Ctrl-C): the TUI **did not quit at all** -
    the filter Input consumed Ctrl-C and the loop kept running (confirmed twice;
    the harness had to SIGKILL it).
  - New-link form field (`2` then `n`, then Ctrl-C) and Import paste (`4`, then
    Ctrl-C): the process **died by SIGINT (shell 130)**, not `0`.
- **Mechanism:** `69114b5` set `ForceHandleCtrlC(false)` and let the global key
  handler fall Ctrl-C through to the focused Input while `text_input_active` is
  true (`src/tui/app_navigation.cpp`). The Input either swallowed Ctrl-C (filter
  -> hang) or ignored it (`handled=false`), so FTXUI's now-un-forced
  `if (CtrlC && !handled)` path re-raised SIGINT (-> 130).
- **Fix (`57cbc39`):** the global handler intercepts Ctrl-C to exit even while a
  text input is focused. Re-tested green in all three contexts: exit `0`,
  termios restored.

---

## Test cases

Each case lists bash and PowerShell commands, the expected result, and a result
row. Commands requiring a non-macOS platform are marked **NOT RUN - requires
`<platform>`**; they were not executed and are not counted as passes.

### S1. Start / quit (D1)

```bash
url_shortener tui --storage-config ./storage.sqlite.yaml
# press Ctrl-C
```

```powershell
.\cmake-build\url_shortener.exe tui --storage-config .\storage.sqlite.yaml
# press Ctrl-C
```

Also: from another shell, `kill -TERM <pid>` and confirm the terminal recovers
(`stty -a` shows `echo`/`icanon`; typing echoes).

**Expected:** the D1 frame (title bar `url_shortener -- backend: ... -- user:
local/Admin [F1 Help]`, the tab row, body, footer); on quit a clean exit code
`0`; shell prompt, cursor and scrollback intact.

**Result:** **PASS** (after BUG-1 fix). Frame matches D1; termios restored after
both Ctrl-C and SIGTERM; Ctrl-C now exits `0`.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | FAIL | Ctrl-C exited 130 (BUG-1); D1 frame + termios restore OK |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | Ctrl-C exits 0; termios restored; SIGTERM restores echo/canonical |
| - | Windows Terminal | - | NOT RUN - requires Windows | |
| - | legacy conhost | - | NOT RUN - requires Windows | |
| - | Linux terminal | - | NOT RUN - requires Linux | |
| - | tmux | - | NOT RUN - `which tmux` empty on this host | |

### S1b. Ctrl-C while a text input is focused

```bash
url_shortener tui --storage-config ./storage.sqlite.yaml
# focus a text input (Links filter via /, a New-link form field, or the
# Import paste area), then press Ctrl-C
```

**Expected:** the TUI exits with code `0` and termios is restored.

**Result:** **PASS** (after BUG-4 fix). Exit `0` + termios restored in all three
contexts. Against `69114b5` this FAILed: the Links filter **hung** (no quit,
force-killed) and the form/paste contexts exited 130 - see BUG-4.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 69114b5 | FAIL | filter: no quit (force-killed); form/paste: exit 130 (BUG-4) |
| 2026-11-19 | macOS pty 80x24 | 57cbc39 | PASS | filter + form + paste: Ctrl-C exits 0, termios restored |

### S2. Minimum size (D1)

```bash
# resize the window (or the harness sets TIOCSWINSZ) to 79x23, then 80x24, then 140x40
url_shortener tui --storage-config ./storage.sqlite.yaml
```

```powershell
# drag the window to <80 cols, then >=80x24, then larger
.\cmake-build\url_shortener.exe tui --storage-config .\storage.sqlite.yaml
```

**Expected:** below 80x24, a centered `terminal too small (need 80x24)`; at and
above 80x24 the frame reflows; no crash across live resizes.

**Result:** **PASS.** 79x23 shows the message; 80x24 and 140x40 render; a live
`SIGWINCH` resize (100x30 -> 79x23 -> 120x40) reflows with no crash.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty | 6660172 | PASS | static 79x23 / 80x24 / 140x40 + live SIGWINCH resize |

### S3. Navigation (D11)

```bash
url_shortener tui --storage-config ./storage.sqlite.yaml
# press 1 2 3 4 5 6 7 then : ; press q from a nested screen
```

**Expected:** `1`-`7` switch tabs per D11, `:` opens the palette, `q` pops a
nested screen; Analytics (`6`) shows the placeholder.

**Result:** **PASS** (after BUG-2 fix). `1`-`7` switch tabs (Dashboard, Links,
Tables, Import, Jobs, Analytics placeholder, Settings), `:` opens the palette;
`q`/Esc pops nested screens (e.g. Links detail -> table). `q` at a
non-Dashboard tab root returns to Dashboard per D11's `Settings --> Dashboard: q`
edge (fixed in `47ac255`; Esc at a tab root stays a no-op). First pass FAILed
(nav dead - BUG-2).

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | FAIL | digits/`:`/`q` swallowed by a shared `text_input_active` (BUG-2) |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | all tabs navigate; nested `q`/Esc back works; tab-level `q`->Dashboard initially a no-op, fixed in `47ac255` (pty: `7` then `q` shows Dashboard) |

### S4. Typing vs hotkeys

```bash
# in the Links screen, press / to focus the filter, then type: 1 2 : q
```

**Expected:** `12:q` appears in the field; no tab switching.

**Result:** **PASS.** The filter shows `filter: [12:q]`; no navigation. (Driven
inside Links, reached via the Dashboard `Enter` workaround, since the Dashboard
nav keys are themselves blocked by BUG-2.)

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | characters land in the Input, not the global hotkey handler |

### F1 help while an Input is focused

**Expected:** F1 opens the help overlay even while a text Input is focused; `?`
remains typable into the Input.

**Result:** **PASS.** F1 (both `ESC O P` and `ESC [ 1 1 ~`) opens the overlay
listing the global keymap; the handler evaluates F1 before the text-input guard.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | overlay shows `F1 / ?  help`, `1-7 switch tab`, `: palette` |

### S5. Dashboard (D2)

**Expected:** link counts equal `link.list` totals per status; sampled labels
visible; no freeze while loading.

**Result:** **PASS.** `Links total: 30 active: 28 disabled: 1 deleted: 1`
matches `link.list`. The Dashboard issues **four** `link.list` count calls
(`status = all|active|disabled|deleted`, `limit:1`, reads `total`) - an as-built
refinement of D2 (see the D-matrix).

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | counts match; redirects/p50/top-slugs render `n/a`/sample (no `stats.summary` command yet) |

### S6. Links table (D3)

**Expected:** filter updates the table; status radio works; `PgDn` pages;
footer shows `page N/M`; selection stays visible.

**Result:** **PASS.** Filter bar and `status: (all) active disabled deleted`
radio render; `PgDn` -> `page 2/2 (20 per page)` with the next rows; the `>`
selection marker stays visible. Links has no sort key by design (`s` = stats).

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | reached via Dashboard `Enter` workaround |

### S7. Create / delete (D5, D10)

**Expected:** `n` opens the create form from the `link.create` ArgSpec with the
`Equivalent:` footer; `d` requires typing the slug to confirm; errors surface as
toasts with the exact `error.code` (e.g. `slug_conflict`).

**Result:** **PASS.** `n` renders the D5 form (`New link (command:
link.create)`, all fields, `[Create] [Cancel] [ ] dry-run`,
`Equivalent: url_shortener cmd link.create --json '{}'`). `d` on `seed-20`
renders the D10 confirm `Soft-delete "seed-20"? Type the slug to confirm:` with
an input box. The duplicate-slug toast was not driven headlessly; it is covered
by the golden `d10_toast_error_80x24.txt` (`! slug_conflict`).

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | form + Equivalent footer + typed-slug delete confirm; conflict toast via golden |

### S8. Link detail / stats (D4)

```bash
# terminal A: a server over the same SQLite file
url_shortener --http-port 38765 --tls-enabled false --storage-config ./storage.sqlite.yaml &
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:38765/seed-01   # 302 x3
# terminal B: open the link detail and press r to refresh stats
url_shortener tui --storage-config ./storage.sqlite.yaml
```

**Expected:** the stats strip shows redirect counts after `r`.

**Result:** **LIMITED / not fully runnable here.** The server redirect returns
`302 -> https://example.com/page/1`, but a separate `link.get`/TUI process reads
`total_redirects: 0`: analytics is best-effort and only `noop`/`inmemory`
backends exist, neither shared across processes, so the TUI cannot observe a
**server** process's click counts. Not a TUI defect - a backend limitation
(there is no cross-process analytics store in Milestone 0006). The redirect and
TUI/server separation themselves pass.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS + curl | 6660172 | LIMITED | 302 OK; cross-process analytics not observable with inmemory/noop backends |

### S9. Tables browser (D6)

**Expected:** browse `links`/`click_events`/`jobs`; search; `Enter` shows row
JSON; `n` on `links` opens the create form; no auth/secret tables or columns.

**Result:** **PASS** (after BUG-2 fix). `3` opens Tables showing exactly
`links` (32 rows), `click_events` (0), `jobs` (0) - no auth/secret tables; the
row columns are `id`/`slug`/`url` with no hash/token/secret columns. (Search,
row-JSON and insert sub-flows covered by goldens `d06_tables_*`.)

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | BLOCKED (BUG-2) | goldens cover layout |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | only links/click_events/jobs; no secret tables/columns |

### S10. Import / export (D7, D8)

**Expected:** paste rows (one blank slug), dry run then real; skip/fail/
overwrite conflict modes; export CSV; a job appears in Jobs with progress;
`Ctrl-S` starts the import.

**Result:** **PASS** (after BUG-2 fix). `4` opens the D7 import form - source
(paste / file path), format (CSV / NDJSON), on-conflict (skip / fail /
overwrite), the paste box with the example rows (one blank slug), dry-run and
Start; `F2` toggles to Export; `Ctrl-S` starts (see S11). Conflict-mode and
export sub-flows covered by goldens `d07_*`.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty | 6660172 | BLOCKED (BUG-2) | goldens cover layout |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | D7 import form renders; Ctrl-S submit is the as-built binding |

### S11. Jobs (D8)

**Expected:** cancel a running large import (>= 5,000 lines); state `cancelled`;
partial effects reported; polling stops when idle (CPU ~0).

**Result:** **PASS.** A ~5,000-row CSV takes ~14.5 s to import (measured), so it
is cancellable. Driving the TUI (Import tab -> file-path source -> path ->
`Ctrl-S`; then Jobs tab -> `c` cancel) produced job `j-1 link.import` with
`state: cancelled`, `done: 972`, and 972 links actually created - partial
effects reported, matching D8. The "polling stops / CPU ~0 when idle" sub-check
was not measured here.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | 5,000-row file-path import cancelled -> `cancelled`, 972/5000 partial |

### S12. Command palette (D9)

**Expected:** `:` opens the palette; typing `link.` lists catalog commands;
`system.info`/`command.list`/a write command run and show in a viewer.

**Result:** **PASS** (after BUG-2 fix). `:` opens the palette; typing `link.`
filters to the `link.*` catalog (`link.create ... write`, `link.delete ... write
(confirm)`, `link.export ... read (jobs)`, etc.) with permission labels. Running
a command and the result viewer are covered by goldens `d09_result_view`.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty | 6660172 | BLOCKED (BUG-2) | goldens cover layout |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | `:` opens palette; `link.` filters the catalog with permission labels |

### S13. Read-only mode

```bash
url_shortener tui --read-only --storage-config ./storage.sqlite.yaml
```

```powershell
.\cmake-build\url_shortener.exe tui --read-only --storage-config .\storage.sqlite.yaml
```

**Expected:** a `READ-ONLY` badge; write controls disabled with a reason, not
hidden; attempting a write sends nothing.

**Result:** **PASS.** Title shows `user: local/User [READ-ONLY]`. As-built
display rules (ratified 2026-11-19): Dashboard `total`/`deleted`/redirects show
`n/a (read-only)` while `active`/`disabled` still load; the Links `all`/`deleted`
status choices render disabled. Nothing hidden.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | badge + `n/a (read-only)` counts |

### S14. Mono / ASCII theme

```bash
url_shortener tui --theme mono --storage-config ./storage.sqlite.yaml
```

```powershell
# in legacy conhost with a raster font
.\cmake-build\url_shortener.exe tui --theme mono --storage-config .\storage.sqlite.yaml
```

**Expected:** only ASCII glyphs (borders folded to `+`/`-`/`|`), layout intact,
including overlays.

**Result:** **PASS.** The frame and the F1 help overlay render pure ASCII; a
non-ASCII scan of the rendered frame is empty. conhost/raster-font pass is
**NOT RUN - requires Windows**.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | overlay also ASCII |
| - | conhost raster font | - | NOT RUN - requires Windows | |

### S15. Slow / unavailable backend

**Expected:** the UI stays responsive (keys echo, can quit); timeouts shown as
toasts.

**Result:** **NOT RUN - requires a slow/unavailable Postgres or a sleep-
injecting test backend**, neither available on this host.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | - | 6660172 | NOT RUN | no slow backend available |

### S16. Contract C5 - no listener, no `uri.txt`

```bash
url_shortener tui --storage-config ./storage.sqlite.yaml &
pid=$!; sleep 2
lsof -nP -a -p "$pid" -iTCP -sTCP:LISTEN   # expect: empty
ls uri.txt                                  # expect: missing
```

```powershell
# Windows:
Get-NetTCPConnection -State Listen | Where-Object OwningProcess -eq (Get-Process url_shortener).Id
Test-Path .\uri.txt
```

**Expected:** no listening port owned by the process; no `uri.txt` in the cwd.

**Result:** **PASS.** `lsof` returns no LISTEN socket for the TUI pid; the cwd
holds only `url_shortener-tui.log` (expected), no `uri.txt`. The PowerShell
`Get-NetTCPConnection` variant is **NOT RUN - requires Windows**.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS + lsof | 6660172 | PASS | 0 listen sockets; no uri.txt |
| - | Windows PowerShell | - | NOT RUN - requires Windows | |

### S17. Persistence across restart

```bash
cfg=./storage.sqlite.yaml
url_shortener tui --storage-config "$cfg"     # note the total, quit
url_shortener tui --storage-config "$cfg"     # same total persists
url_shortener tui                             # in-memory: banner is shown
```

**Expected:** with SQLite the links survive a restart; with the in-memory
backend the `memory store - data is lost on exit` banner is shown.

**Result:** **PASS.** Two SQLite restarts both read `total: 30`; a write via the
shared runtime makes the next launch read `total: 31`. The in-memory launch
(`backend: inmemory`) shows the banner.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | 30 -> 30 -> 31; in-memory banner present |

### S18. Non-TTY input / output

```bash
url_shortener tui < /dev/null ; echo "exit=$?"     # expect exit 2
url_shortener tui | cat ; echo "exit=${PIPESTATUS[0]}"
```

```powershell
.\cmake-build\url_shortener.exe tui < NUL ; $LASTEXITCODE
.\cmake-build\url_shortener.exe tui | more
```

**Expected:** exit `2` with a one-line message, no garbage.

**Result:** **PASS** (after BUG-3 fix). Both exit `2`; stdout empty (0 bytes);
stderr carries **exactly one** line, `tui: refusing to start without an
interactive terminal`. First pass had an extra `job_runner_shutdown` INFO line
(BUG-3).

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS | 6660172 | PASS* | exit 2; clean stdout; extra INFO line on stderr (BUG-3) |
| 2026-11-19 | macOS | 69114b5 | PASS | exit 2; stdout empty; exactly one stderr line |

### S19. Built without TUI (OFF build)

```bash
cmake -S . -B cmake-build-notui -G Ninja -DURLSHORTENER_ENABLE_TUI=OFF
cmake --build cmake-build-notui
cmake-build-notui/url_shortener tui ; echo "exit=$?"   # expect 2
```

**Expected:** exit `2`, message "built without TUI support".

**Result:** **NOT RUN (optional).** A separate OFF reconfigure/build was not
performed (keep `cmake-build` config untouched; a fresh OFF build pulls the full
FetchContent tree). The message and exit are verified in source:
`src/main.cpp:226-228` emits `built without TUI support (URLSHORTENER_ENABLE_TUI=OFF)`
and returns `2`.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | - | 6660172 | NOT RUN | optional; behavior confirmed by source inspection |

### S20. Unicode / CJK

```bash
# target URLs / metadata carry CJK (tags reject non-ASCII by design)
url_shortener cmd link.create --storage-config "$cfg" \
  --json '{"target_url":"https://example.com/世界/café","slug":"cjk-url"}'
url_shortener tui --storage-config "$cfg"   # view the Links table
```

**Expected:** alignment holds; no truncated multibyte garbage.

**Result:** **PASS.** The `cjk-url` row renders
`https://example.com/世界/café` with `active`/`0`/`-` aligned to the ASCII row
above - the table uses display-width-aware fitting. CJK **tags** are rejected by
validation (`invalid_field: tags violate constraints`), so that specific field
path cannot carry CJK.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS pty 80x24 | 6660172 | PASS | wide-char columns aligned; CJK tags rejected (expected) |

### S21. Settings (D13)

```bash
url_shortener tui --storage-config ./storage.sqlite.yaml
# press 7 ; compare each field to the startup --storage-config / CLI flags ; press q
```

**Expected:** values match the startup config exactly; no TLS path, port, or
`analytics_client_hash_salt` appears; `q` returns to the Dashboard; no write
control on this screen.

**Result:** **PASS** (after BUG-2 fix). `7` opens the live D13 Settings screen:
Version 1.0.0, `Read-only session no`, Backend (metadata sqlite persistent, cache
inmemory, analytics inmemory, rate limit disabled), Shortener (base domain,
generated slug length 7, default redirect type temporary, default expiry never,
allow private targets false, analytics enabled true), Request hardening (body
65536 / target 2048 / id 64) - every value matches the startup config. No TLS
path, port, or `analytics_client_hash_salt` appears; a secret-scan of its
`system.info` data source is empty. `q`/digits leave the screen; it has no write
control.

| Date | Terminal | Build | Pass/Fail | Notes |
|---|---|---|---|---|
| 2026-11-19 | macOS + cmd | 6660172 | PARTIAL | live blocked by BUG-2; data source secret-free |
| 2026-11-19 | macOS pty 80x24 | 69114b5 | PASS | live D13 screen; fields match config; no secrets |

---

## D1-D13 coverage matrix

Each diagram maps to its checklist item(s), the as-built deviation (if any; see
the [ADR 0003 amendments](/docs/adr/0003-tui-framework-and-architecture.md)),
and the automated golden(s) under `tests/unit/tui/golden/`.

| Diagram | Checklist item(s) | As-built deviation | Golden file(s) |
|---|---|---|---|
| **D1** Frame & nav | S1, S2, S3, S13, S14 | `[F1 Help]` trails the title (FTXUI sizes the window title to its width; right-corner alignment is not available); `[READ-ONLY]` badge in the title | `d01_frame_80x24`, `d01_frame_light_80x24`, `d01_frame_mono_80x24`, `d01_frame_readonly_80x24`, `d01_frame_memory_banner_80x24`, `d01_frame_narrow_60x20` |
| **D2** Dashboard | S5, S13 | Dashboard makes **4** `link.list` count calls (all/active/disabled/deleted, `limit:1`); redirects-24h / success% / p50 / top-slugs are `n/a` or a sample (no `stats.summary` command exists); read-only shows `n/a (read-only)` for total/deleted/redirects | `d02_dashboard_80x24`, `d02_dashboard_memory_80x24`, `d02_dashboard_mono_80x24`, `d02_dashboard_partial_failure_80x24`, `d02_dashboard_stats_summary_80x24` |
| **D3** Links table | S4, S6, S13 | Hosted inline; read-only defaults to status `active`, `all`/`deleted` radios disabled | `d03_links_80x24`, `d03_links_filter_80x24`, `d03_links_empty_80x24`, `d03_links_error_80x24`, `d03_links_readonly_80x24`, `d03_table_20x_80x24` |
| **D4** Link detail / stats | S8 | **Detail/stats hosted inline in the Links parent screen**, not a separate screen node | `d04_detail_80x24`, `d04_detail_deleted_80x24`, `d04_detail_no_stats_80x24`, `d04_detail_stats_error_80x24`, `d04_detail_mono_80x24` |
| **D5** New/edit form | S7 | **Form hosted inline in the parent screen**; generic form submit is `Ctrl-S` and `Ctrl-R` toggles the raw-JSON args editor | `d05_new_link_80x24`, `d05_new_link_errors_80x24`, `d05_new_link_readonly_80x24`, `d05_edit_link_80x24`, `d05_form_scroll_80x12`, `d05_generic_enum_dropdown_80x24` |
| **D6** Tables browser | S9 | **Row-JSON hosted inline**; insert (`n`) only on `links`; no auth/secret tables | `d06_tables_80x24`, `d06_tables_events_80x24`, `d06_tables_row_json_80x24`, `d06_tables_empty_80x24`, `d06_tables_error_80x24`, `d06_tables_mono_80x24` |
| **D7** Import / export | S10 | `Ctrl-S` submits the import; read-only disables the write controls | `d07_import_80x24`, `d07_import_file_80x24`, `d07_import_readonly_80x24`, `d07_import_toolarge_80x24`, `d07_export_80x24` |
| **D8** Jobs | S10, S11 | - | `d08_jobs_80x24`, `d08_jobs_empty_80x24`, `d08_jobs_mono_80x24`, `d08_job_detail_failed_80x24` |
| **D9** Command palette | S12 | Generic command flow: `Ctrl-S` run, `Ctrl-R` toggles raw-JSON (not `Ctrl-J`) | `d09_palette_empty_80x24`, `d09_palette_link_prefix_80x24`, `d09_palette_readonly_80x24`, `d09_result_view_80x24` |
| **D10** Confirm / toast | S7 | - | `d10_confirm_delete_80x24`, `d10_confirm_yesno_80x24`, `d10_toast_error_80x24`, `d10_toast_details_80x24` |
| **D11** Screen state machine | S3 | Live transitions PASS after the BUG-2 fix (tabs by `1`-`7`/`:`; nested `q`/Esc back). Tab-level `q`->Dashboard edge implemented in `47ac255` (`tui__20_app_navigation_test`). | (no single golden; router unit tests `tui__04_*`) |
| **D12** Sequence (one create) | S5, S7 | **Snapshot-only** (justified): a wire sequence, not a screen. The request envelope is pinned by the in-process-client request goldens | `07_request_link_create.json`, `07_request_link_list_all.json`, `07_request_link_update_clear.json`, `12_request_link_list_active.json`, `12_request_link_list_all.json` |
| **D13** Settings | S21 | Always read-only (even for Admin) by design; TLS/ports/salt never read; data source `system.info` | `d13_settings_80x24`, `d13_settings_readonly_80x24`, `d13_settings_mono_80x24`, `d13_settings_unavailable_80x24` |

Every diagram has at least one passing manual item or an explicit snapshot-only
justification (D11, D12). D11's live navigation currently FAILS (BUG-2) and must
be re-checked once that is fixed.

---

## Known limitations

- **Platform coverage:** only macOS was available. Windows Terminal, legacy
  conhost, Linux terminals/WSL, `tmux`, and PowerShell `Get-NetTCPConnection`
  rows are recorded as **NOT RUN - requires `<platform>`**, never as passes.
  `tmux` is not installed on this host (`which tmux` is empty).
- **Headless pty driver:** the recorded run is driven by a scratch Python `pty`
  harness, not a human at a physical terminal. It sets `TIOCSWINSZ`, streams
  keys, and verifies termios restoration via `stty -a` before/after. It is a
  faithful but not identical substitute for a hands-on terminal; conhost/tmux
  rendering quirks are out of its reach.
- **BUG-2 blast radius (resolved):** the first pass could not exercise
  S9/S10/S11/S12/S21 live because global navigation was dead; after the
  `69114b5` fix all were re-run and pass. The tab-level `q`->Dashboard edge in
  D11 was implemented in `47ac255`.
- **S8 analytics:** no cross-process analytics store exists in Milestone 0006
  (`noop`/`inmemory` only), so a TUI process cannot observe a separate server
  process's redirect counts. S8's stats path is not fully runnable here.
- **S15/S19:** no slow/unavailable backend and no OFF build were set up; S19's
  behavior is confirmed only by source inspection.
- **CJK tags:** `link.create` rejects non-ASCII tags, so S20's Unicode coverage
  is via the target URL and metadata, not tags.
