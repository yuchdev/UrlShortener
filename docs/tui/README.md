# Terminal UI

`url_shortener tui` is an interactive full-screen terminal interface that runs every
command the REST API and CLI can run, plus a database table browser and bulk import /
export. It is a transport adapter over the shared command model: every action is an
[ADR 0002](../adr/0002-unified-command-model.md) `CommandRequest` and the same logic
executes whether you use the TUI, the CLI, or the HTTP API. See
[ADR 0003](../adr/0003-tui-framework-and-architecture.md) for the architecture decisions
behind it.

## Requirements

| Requirement | Detail |
|---|---|
| Terminal size | 80 × 24 minimum; below this a "terminal too small" banner appears |
| Encoding | UTF-8 recommended; `--theme mono` for legacy single-byte consoles |
| Interactive TTY | Both stdin and stdout must be a TTY; the TUI refuses to start on a pipe or redirect with exit code 2 |
| Build option | `URLSHORTENER_ENABLE_TUI` (default `ON`); a binary built with `OFF` reports `tui: subcommand unavailable` |
| Platform | macOS and Linux are the primary tested platforms (manual smoke); Windows, conhost and tmux are not yet manually smoke-tested and are a ratified deferral for a future milestone |

## Starting

```
url_shortener tui [--storage-config <FILE>] [--theme dark|light|mono]
                  [--read-only] [--yes]
                  [--no-file-paths | --path-root <DIR>]
                  [--log-file <PATH>]
```

Flag reference (verified against `url_shortener tui --help`):

| Flag | Default | Description |
|---|---|---|
| `--storage-config <FILE>` | in-memory | Storage configuration YAML; without it the TUI uses an in-memory store |
| `--theme dark\|light\|mono` | `dark` | Colour theme; use `mono` for terminals that do not support colour |
| `--read-only` | off | Session runs as `ControlSet::User`; write controls render disabled with the `not_authorized` reason, never hidden |
| `--yes` | off | Skip mutation confirmation dialogs (useful for scripted or smoke-test runs) |
| `--no-file-paths` | off | Refuse any `path` argument as `unsupported`; mutually exclusive with `--path-root` |
| `--path-root <DIR>` | off | Confine import/export `path` arguments to this absolute directory; paths outside it are refused; mutually exclusive with `--no-file-paths` |
| `--log-file <PATH>` | `./url_shortener-tui.log` | Write logs and audit lines to this file (created 0600, never to the terminal) |

**In-memory store banner.** When no `--storage-config` is given the TUI works within
the session and shows a persistent banner: *"memory store – data is lost on exit"*.
Use a SQLite or PostgreSQL backend (see the CLI reference in [docs/cli/README.md](../cli/README.md))
for persistent data.

**Relation to server mode.** The TUI never starts `HttpServer` or binds a socket.
The CLI, TUI, and server share one backend through `--storage-config`; server and CLI
sharing a SQLite file is covered by integration test `cli_18_server_and_cli_share_sqlite`.
Concurrent writers rely on SQLite's built-in file locking; heavy concurrent write use
has not been benchmarked.

## Screen map

| Tab key | Screen | Main commands used |
|---|---|---|
| `1` | Dashboard | `link.list` (counts), `job.list`, `system.info` |
| `2` | Links | `link.list`, `link.create`, `link.update`, `link.delete`, `link.enable`, `link.disable`, `link.restore`, `link.preview`, `link.stats` |
| `3` | Tables | `db.tables`, `db.rows`, `link.create` (insert) |
| `4` | Import/Export | `link.import`, `link.export` |
| `5` | Jobs | `job.list`, `job.get`, `job.cancel` |
| `6` | Analytics | placeholder (provided by Milestone 0002; no screen beyond the tab) |
| `7` | Settings | `system.info` (read-only view) |
| `:` | Command Palette | every registered command |

The Analytics tab (`6`) is a placeholder in the current release. It has no command and
no screen body; this area is owned by a future milestone.

## Navigating

**Global hotkeys** (active on every screen unless a text field has focus):

| Key | Action | Source |
|---|---|---|
| `1`–`7` | Switch to that tab | `src/tui/app_navigation.cpp` line 106 |
| `:` | Open Command Palette | `src/tui/app_navigation.cpp` line 106 |
| `q` | Back one level; from a tab root returns to Dashboard; no-op on Dashboard | `src/tui/app_navigation.cpp` lines 109–123 |
| `Esc` | Back one level (same as `q` for navigation) | `src/tui/app_navigation.cpp` lines 97–99 |
| `Ctrl-C` | Quit the TUI cleanly (exit 0) | `src/tui/app_navigation.cpp` lines 88–90 |
| `F1` | Open / close the help overlay | `src/tui/app_navigation.cpp` lines 78–81 |
| `?` | Open / close the help overlay (when no text field is focused) | `src/tui/app_navigation.cpp` lines 102–105 |

**Focus rules.** When a text input field (filter bar, form field, paste area) is focused
it captures printable keys and prevents tab-digit navigation. The screen sets
`ScreenContext::text_input_active` when this is true; global hotkeys `F1` and `Ctrl-C`
bypass this gate and are always reachable.

**Help overlay.** `F1` or `?` opens a small overlay listing the keys above plus the
current screen's own hints. `Esc`, `F1`, or `?` closes it.

## Links

The Links tab (D3) displays a paginated, filterable, sortable table of short links.

**Filter bar.** The `/` key opens the filter bar. Tokens are classified:

| Token form | Meaning |
|---|---|
| `camp:<name>` | Filter by campaign name |
| `tag:<name>` | Filter by tag |
| `<other text>` | Free-text search (`q` parameter) |

Multiple tokens may be combined; the last `camp:` / `tag:` wins when duplicated.

**Status radio.** The status bar shows `all  active  disabled  deleted`. Press `Tab`
(when no text field is active) to cycle; in a `--read-only` session the `all` and
`deleted` radios are dimmed because those views require write permission.

**Row actions** (when the table row is selected, no text field focused):

| Key | Action | Source |
|---|---|---|
| `Enter` | Open inline Link detail / stats (D4) | `src/tui/screens/links_screen.cpp` line 134 |
| `n` | Open new-link form (`link.create`) | `src/tui/screens/links_screen.cpp` line 398 |
| `e` | Open edit form (`link.update`) prefilled | `src/tui/screens/links_screen.cpp` line 402 |
| `t` | Toggle enable/disable (`link.enable` or `link.disable`) | `src/tui/screens/links_screen.cpp` line 406 |
| `d` | Delete link (`link.delete`) – opens typed-slug confirm | `src/tui/screens/links_screen.cpp` line 410 |
| `u` | Restore deleted link (`link.restore`) | `src/tui/screens/links_screen.cpp` line 414 |
| `p` | Preview resolved URL (`link.preview`) | `src/tui/screens/links_screen.cpp` line 418 |
| `s` | Show stats / detail view (`link.stats`) | `src/tui/screens/links_screen.cpp` line 422 |

**Link detail / stats (D4).** Press `Enter` or `s` on a row to open an inline detail
view showing the link's metadata, its short URL, and a 7-day stats bar. Press `e` from
the detail to open the edit form; `Esc` or `q` returns to the table.

**Create / edit form (D5).** A generated form from the `link.create` / `link.update`
`ArgSpec` list. Fields:

- `target_url` (required for create) – the destination URL
- `slug` – leave blank to generate one automatically
- `redirect` – radio: `permanent` or `temporary`
- `expires_at` – ISO-8601 datetime or blank for never
- `enabled` – checkbox
- `tags` – comma-separated list
- `metadata` – `key=value` pairs, comma-separated
- `campaign` – sub-fields: name, source, medium

The footer shows the **Equivalent** `url_shortener cmd` command for the current form
state, so the TUI and CLI are demonstrably identical.

Validating `target_url` and `slug` happens server-side; client-side checks are
advisory only. Error codes from the server (e.g. `slug_conflict`, `invalid_url`)
appear as toast notifications.

**Paging.** Results are shown 20 per page; the table model handles offset/limit
automatically.

## Tables

The Tables tab (D6) is a two-pane browser over the storage port's logical tables.

Left pane – table list (use `j`/`k` or arrow keys to select; `Enter` focuses the
right pane):

| Table | Rows | Writable |
|---|---|---|
| `links` | link records | yes (`link.create` via `n`) |
| `click_events` | analytics events | no |
| `jobs` | background jobs | no |

Authentication tables and any column whose name looks like a secret (contains
"password", "hash", "token", "salt", or "key") are hidden from both the column
list and the row-JSON viewer.

**Right pane hotkeys:**

| Key | Action | Source |
|---|---|---|
| `Enter` | Open row JSON viewer | `src/tui/screens/tables_screen.cpp` line 379 |
| `s` | Cycle sort column / direction | `src/tui/screens/tables_screen.cpp` line 447 |
| `n` | Insert row (links table only → opens D5 form) | `src/tui/screens/tables_screen.cpp` line 451 |
| `c` | Toggle column chooser | `src/tui/screens/tables_screen.cpp` line 793 |
| `/` | Search (when the table has a searchable column) | filter bar |
| `Tab` | Switch focus between left and right panes | `src/tui/screens/tables_screen.cpp` line 219 |

Row-JSON viewer keys: `j`/`k` or arrows to scroll, `PageDown`/`PageUp` to page,
`Esc` to return to the browser.

## Import and export

The Import / Export screen (D7) runs `link.import` and `link.export` as background
jobs. Press `F2` to toggle between the Import and Export sub-tabs.

**Import sub-tab:**

| Field | Detail |
|---|---|
| Source | `paste` (type or paste inline) or `file path` (the server reads the file) |
| Format | `CSV` (`full_url,slug` per line, blank slug → generated) or `NDJSON` (one `link.create` args object per line) |
| On conflict | `skip` (default), `fail`, or `overwrite` (destructive) |
| Paste area | Visible when source is `paste`; example placeholder shown when empty |
| Dry run | Checkbox; validates without importing |

CSV example:

```csv
https://example.com/docs,docs
https://example.com/launch,
https://example.com/promo,promo
```

NDJSON example (one JSON object per line):

```ndjson
{"target_url":"https://example.com/docs","slug":"docs"}
{"target_url":"https://example.com/launch"}
```

**Export sub-tab:** choose format (CSV or NDJSON), status filter, optional `q`/tag/
campaign filters, and an optional file path. Leaving `file path` blank returns the data
inline in the Jobs detail envelope.

**Key bindings:**

| Key | Action | Source |
|---|---|---|
| `Ctrl-S` | Start import or export | `src/tui/screens/import_export_screen.cpp` line 216 |
| `F2` | Toggle between Import and Export sub-tabs | `src/tui/screens/import_export_screen.cpp` line 212 |
| `Tab` / `Shift-Tab` | Move focus between controls | `src/tui/screens/import_export_screen.cpp` lines 221–226 |

After `Start import`, the TUI automatically navigates to the Jobs screen and
focuses the new job.

**Size limits.** The inline paste buffer is capped at the server's
`--max-request-body-bytes` limit (default 65 536 bytes). Larger imports must use a
file path.

**CSV-injection note.** The TUI does not post-process CSV output; the server applies
its own value sanitisation before writing. Treat exported CSV as untrusted when opening
in spreadsheet software and enable import-warning prompts.

## Jobs

The Jobs screen (D8) shows all background jobs (`job.list`) and auto-refreshes every
second while any job is running.

**Job states:** `queued`, `running`, `succeeded`, `failed`, `cancelled`.

**Hotkeys:**

| Key | Action | Source |
|---|---|---|
| `Enter` | Open full job-detail envelope | `src/tui/screens/jobs_screen.cpp` line 117 |
| `c` | Cancel selected running job | `src/tui/screens/jobs_screen.cpp` line 121 |
| `r` | Manual refresh | `src/tui/screens/jobs_screen.cpp` line 125 |
| `Esc` | Close detail / back | `src/tui/screens/jobs_screen.cpp` line 420 |

Detail-view keys: `j`/`k` or arrows to scroll, `r` to refresh, `Esc` to return
to the list.

**Auto-refresh.** The Jobs screen polls `job.list` at 1 Hz while it is the active
tab and any job is running. Polling stops automatically once all jobs are terminal.
When you navigate away the ticker stops unless a job is still active.

**Retention.** Jobs are in-memory only. Exiting the process clears all job records.

## Command palette

Press `:` from any screen to open the Command Palette (D9). Start typing to filter
commands by name or description.

Selecting a command opens the generated form for its `ArgSpec` list (the same layout as
D5). Every command registered in the catalog is reachable here with zero extra TUI code:
the form is generated automatically from the descriptor.

**Key bindings in the Command Palette / generic form:**

| Key | Action | Source |
|---|---|---|
| `Ctrl-S` | Submit the current form | `src/tui/screens/generic_command_screen.cpp` line 149 |
| `Ctrl-R` | Toggle raw-JSON args editor | `src/tui/screens/generic_command_screen.cpp` line 144 |
| `y` | Copy the equivalent CLI command to a toast | `src/tui/screens/generic_command_screen.cpp` line 289 |
| `Esc` | Close and return to the palette list (raw-mode handler at line 153; result-viewer close at line 285) | `src/tui/screens/generic_command_screen.cpp` lines 153, 285 |

**Raw-JSON editor.** `Ctrl-R` replaces the generated form with a raw-text editor
pre-seeded with the current form's args object. Edit the JSON directly and press
`Ctrl-S` to submit. `Ctrl-R` again returns to the generated form. The editor validates
that the buffer is exactly one JSON object before submitting.

**Typed-slug confirm.** Commands with `mutating = true` show a yes/no confirm dialog
before the request is sent (skippable with `--yes`). `link.delete` requires typing the
exact slug to confirm; this is enforced at the presentation layer and cannot be
bypassed by `--yes` for that specific command.

## Read-only mode

Launching with `--read-only` runs the session as `ControlSet::User`. The session
cannot issue write commands.

What changes in read-only mode:

- The `[READ-ONLY]` badge appears in the frame header beside the backend line.
- Write action keys (`n`, `e`, `t`, `d`, `u`) in the Links table render dimmed.
- The `all` and `deleted` status radios in Links and in the Export status filter
  are dimmed (those views require write permission for `link.list` / `link.export`).
- The export `file path` field is disabled (path export requires write).
- Attempting a write from the Command Palette shows a `not_authorized: read-only
  session` toast and sends zero requests.
- The Settings screen (D13) is always read-only regardless of session type; it
  shows the server configuration and has no edit controls.

Controls are **never hidden** in read-only mode; they render disabled with the
`not_authorized` reason so the operator can see what would be available in a
write session. This is ADR 0003 rule 5.

## Errors

Errors appear as toast notifications at the bottom of the screen.

**Toast anatomy:**

```
+- ! slug_conflict ----------------------+
| slug "docs" already exists.            |
| (link.create, id tui-7)   [d]etails   |
+----------------------------------------+
```

Press `d` on an error toast to open the full response envelope in a scrollable viewer.

**Common error codes:**

| Code | Meaning | What to do |
|---|---|---|
| `slug_conflict` | A link with that slug already exists | Choose a different slug or use `on_conflict=overwrite` in Import |
| `invalid_url` | The target URL failed validation (private host, bad scheme, etc.) | Check the URL; use `--allow-private-targets` if appropriate |
| `not_authorized` | Session lacks write permission | Run with `--read-only` removed, or contact the administrator |
| `busy` | The request queue is full | Wait a moment and retry |
| `storage_failure` | An unrecoverable storage error | Check the storage backend; see the operations runbook |

## Troubleshooting

**Garbled characters / no colour.** Use `--theme mono`. This switches to ASCII box
characters and disables colour. Useful for terminals without full VT100 / UTF-8 support
and for some tmux configurations.

**Terminal appears blank or corrupt after exit.** The TUI uses an alternate screen
buffer and an RAII guard restores the terminal on exit, including on `Ctrl-C`. If the
process is killed with `SIGKILL` the guard cannot run; run `reset` or `tput reset` to
restore the terminal.

**"terminal too small" message.** The TUI requires at least 80 × 24. Resize the
terminal and the message will clear.

**tmux.** Ensure your tmux configuration forwards true-colour or use `--theme mono`.
Window resizing inside tmux is fully supported.

**SQLite "database is locked".** Only one writer may hold the SQLite file at a time.
If a server process has the file open with writes pending, the TUI may see transient
lock errors. These surface as `storage_failure` toasts; they are transient and usually
resolve within a second.

**Stuck job.** A job in `running` state that never progresses may be from a previous
session (jobs are in-memory and do not survive a process restart). Navigate to the
Jobs screen, select the job, and press `c` to cancel.

**Analytics tab shows nothing.** The Analytics tab is a placeholder in the current
release; analytics deep-dives are provided by a future milestone. See the [roadmap](../roadmap/README.md).

## Security

The TUI operates under the **local operator trust model**: it runs on the same machine
as the storage backend and is not exposed to the network. No HTTP server is started.

- Secrets (passwords, tokens, hashes, salts) are never displayed. The Tables browser
  hides columns with secret-like names and redacts secret-named fields from the row-JSON
  viewer. The `db.*` command handlers enforce this server-side; the TUI's table model
  (`src/tui/tables_model.cpp`) redacts again as defense in depth.
- The `--log-file` file is created with mode 0600 and audit lines for every mutation
  are written there. The log is never written to the terminal.
- `path` arguments (import/export file paths) are confined by `--path-root` or
  rejected entirely by `--no-file-paths`. Both `path` forms require write permission;
  a `--read-only` session cannot use `path` regardless of flag.
- The auth broker is unrelated to `tui` mode: the TUI never starts the auth broker and
  the broker must never be exposed to the public internet (see
  [ADR 0001](../adr/0001-cli-rest-shared-command-layer.md)).

## See also

- [Command catalog](../commands/README.md) – all registered commands and their
  `ArgSpec` schemas
- [CLI reference](../cli/README.md) – the `url_shortener cmd` one-shot interface
- [ADR 0003 – TUI framework and architecture](../adr/0003-tui-framework-and-architecture.md)
- [ADR 0002 – Unified command model](../adr/0002-unified-command-model.md)
- [TUI development guide](development.md) – how to contribute screens and commands
- [Manual smoke checklist](../testing/tui_manual_smoke.md)
- [TUI test plan](../testing/testplans/tui.md)
