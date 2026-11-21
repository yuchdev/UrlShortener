# URL Shortener CLI Reference

The `url_shortener` binary exposes operations through four local modes, all over
the same command registry ([`docs/commands/README.md`](../commands/README.md)):
`link <verb>` (sugar shortcuts for the nine legacy link commands), `cmd <command>`
(generic one-shot runner), `serve-stdio` (NDJSON transport for a parent process),
and `tui` (interactive terminal UI). The full REST inventory and field-by-field
response documentation is in [`docs/api/README.md`](../api/README.md); this
reference does not duplicate those descriptions.

## Storage and backend selection

By default the CLI uses the in-memory link store (`linkRepository()`), a
per-process singleton. **Two separate CLI invocations with the default backend
do not share state.** A link created by `link create` in one process invocation
is invisible to `link get` in a separate invocation; only calls within the *same*
process invocation observe each other. The default cases in the `cli_03` test
suite assert this isolation and remain valid.

### Global options

The following option applies to all nine `link <verb>` commands and may appear
anywhere among the verb's flags:

| Option | Description |
|--------|-------------|
| `--storage-config <FILE>` | Path to a storage configuration YAML file. Selects the backend for this invocation. Overrides `URL_SHORTENER_STORAGE_CONFIG`. Default: in-memory. |

**Selection precedence** (highest to lowest):

1. `--storage-config <FILE>` flag on the command line.
2. `URL_SHORTENER_STORAGE_CONFIG` environment variable.
3. Built-in in-memory default (no config path set).

An inherited `URL_SHORTENER_STORAGE_CONFIG` silently switches the backend for
every CLI invocation in that shell session, including one-off commands that do
not expect persistence (threat model T-41). Set it deliberately and unset it
when done.

`--storage-config` is a *reserved* global token: it is stripped from the
argument list before the per-verb parser runs, so it cannot accidentally be
parsed as another verb's flag value.

### Sharing a backend with the server

Pass `--storage-config <same-yaml>` to both the server and the CLI to share
one persistent backend. Useful for one-off admin operations (`link disable`,
`link delete`) against a running server without needing the REST API.

**Example — SQLite backend, two separate invocations:**

Assuming `storage.yaml` contains:

```yaml
metadata:
  backend: sqlite
  sqlite_path: /var/lib/shortener/links.db
```

```bash
url_shortener link create \
  --storage-config storage.yaml \
  --url https://example.com/docs \
  --slug docs

url_shortener link get \
  --storage-config storage.yaml \
  --slug docs
```

The second command finds the link created by the first because both use the
same SQLite database. Use an **absolute path** for `sqlite_path` when running
CLI commands from different working directories; a relative path is resolved
against the process working directory at startup.

**Cross-process visibility:** a change made by the CLI (e.g. `link disable`)
becomes visible to a running server within one L1 cache TTL — at most **5 s**
for persistent backends, independent of redirect traffic rate.

**Redirect counters (write-behind):** redirect counts are flushed to the
database approximately every **30 s** and at server shutdown. `link stats`
reads in-process click events accumulated since the last flush; it will show
empty stats immediately after a server restart until the first redirect is
recorded and flushed. Stats returned across separate processes reflect the
last persisted snapshot only (limitation R-3).

**Race note:** a concurrent server stats flush can overwrite a `disable` or
soft-delete committed by a CLI command in its read→write window (cross-process
race R-1, accepted limitation). Within a single process, flush-vs-update races
are serialized. See [decision-log D12](
../roadmap/0006-unified-command-model-and-tui/decision-log.md) for the full
analysis.

**Security:** the YAML file may hold database credentials (a Postgres DSN).
Keep its permissions tight (`0600`, owned by the service account) and **never
commit it to source control**. See
[`docs/storage/configuration.md`](../storage/configuration.md) for the full
permissions guidance.

## Invocation

### link \<verb\> (sugar)

```
url_shortener link <verb> [flags...]
```

On success (exit 0), a single JSON object (the `result` field only, legacy
format) is written to **stdout**. On failure (exit > 0), a diagnostic line is
written to **stderr** with stdout empty — suitable for scripting with `$?`
alone:

```bash
url_shortener link create --url https://example.com/docs --slug docs | jq '.slug'
```

Flags compile to the same `args` object as `cmd <name> --json`; `link <verb>`
output is the `result` field only (backward-compatible), while `cmd` prints the
full response envelope. See [`docs/commands/README.md`](../commands/README.md)
for the full command catalog.

### cmd

```
url_shortener cmd <name> [options]
```

One-shot generic command runner. Prints the full response envelope on stdout,
then exits. The command name is a dotted identifier such as `link.create`.

| Flag | Description | Default |
|------|-------------|---------|
| `--json <TEXT>` | Args as a JSON object. Use `--json -` to read from stdin (keeps secrets out of `argv`). Mutually exclusive with `--arg`. | - |
| `--arg <NAME=VALUE>` | One typed argument; repeatable, order preserved. Mutually exclusive with `--json`. | - |
| `--id <TEXT>` | Correlation id echoed in the response. | `cli-1` |
| `--raw` | Print only the envelope's `result` field. | off |
| `--dry-run` | Validate without applying (`options.dry_run`). | off |
| `--async` | Request asynchronous execution (`options.async`). | off |
| `--no-wait` | Do not wait for an asynchronous job; print the queued envelope and exit. | off |
| `--read-only` | Run the local session as a read-only user. | off |
| `--log-file <PATH>` | Write logs to a file. Mutually exclusive with `--verbose`. | disabled |
| `--verbose` | Write logs to stderr. | off |
| `--base-domain <URL>` | Base domain for generated short URLs. | - |
| `--allow-private-targets <BOOL>` | Permit private/intranet targets (`true`/`false`). | - |
| `--storage-config <FILE>` | Storage configuration YAML. | in-memory |

**Default correlation id.** When `--id` is omitted the `cmd` mode assigns
`"cli-1"`.

**Output shape.** By default `cmd` prints the full response envelope:
`{"v":1,"id":"cli-1","ok":true,"result":{...}}`. With `--raw` only the
`result` object is printed. Exit code is derived from `error.code` per the
table below.

**Error behavior:**

- An invalid command name (syntactically wrong, e.g. `LINK.CREATE`) →
  `invalid_envelope` on stdout with exit 2; the operand is never echoed.
- A malformed `--arg` token (no `=` separator, e.g. `--arg foo`) →
  `invalid_envelope` on stdout with exit 2.
- A usage error (unknown option, missing value, etc.) →
  `cmd failed: invalid_request: <reason>` on **stderr** (not stdout), exit 2.

**Examples:**

```bash
# Full envelope output
url_shortener cmd link.create \
  --json '{"target_url":"https://example.com/docs","slug":"docs"}'

# result only
url_shortener cmd system.info --raw

# args via repeated --arg flags
url_shortener cmd link.list --arg status=active --arg limit=10

# read the args object from stdin (keeps secrets out of argv)
echo '{"target_url":"https://example.com","slug":"ex"}' \
  | url_shortener cmd link.create --json -
```

### serve-stdio

```
url_shortener serve-stdio [options]
```

NDJSON transport: one request envelope per stdin line, one response envelope
per stdout line, strictly in order. Logs go to stderr (never stdout). EOF on
stdin drains in-flight jobs and exits `0`. See
[`docs/commands/adding-a-transport.md`](../commands/adding-a-transport.md) for
the full framing rules, shutdown contract, and IPC design sketch.

| Flag | Description | Default |
|------|-------------|---------|
| `--read-only` | Run as a read-only user (`ControlSet::User`). | admin |
| `--no-file-paths` | Reject `link.import`/`link.export` `path` arguments (returns `unsupported`). Mutually exclusive with `--path-root`. | off |
| `--path-root <DIR>` | Confine `link.import`/`link.export` `path` to this directory. Mutually exclusive with `--no-file-paths`. | off |
| `--base-domain <URL>` | Base domain for generated short URLs. | - |
| `--allow-private-targets <BOOL>` | Permit private/intranet targets. | - |
| `--storage-config <FILE>` | Storage configuration YAML. | in-memory |

Exit codes: `0` clean shutdown (EOF after drain); `2` startup error (e.g.
invalid `--path-root`); `4` stream failure.

**Security posture.** By default `serve-stdio` grants `ControlSet::Admin` and
full filesystem access, because the assumed peer is the local operator (the
parent process that launched it). For an untrusted or sidecar peer (e.g. a
webview-driven parent), always pass both `--read-only` and `--no-file-paths`.

**Example:**

```bash
echo '{"v":1,"id":"c-1","command":"link.create",' \
'"args":{"target_url":"https://example.com/docs","slug":"docs"}}' \
  | url_shortener serve-stdio
```

### tui

```
url_shortener tui [--storage-config <FILE>] [--theme dark|light|mono]
                  [--read-only] [--yes] [--log-file <PATH>]
                  [--no-file-paths | --path-root <DIR>]
```

Launches the interactive terminal UI over the configured storage backend. It
runs the **same** commands as the REST API through the one shared command
registry over the active backend, so it reads and writes the same store every
other mode uses. No HTTP/HTTPS server is started, no socket is bound, and no
`uri.txt` is touched. The mode requires an interactive terminal; run against a
pipe or redirect it exits `2`.

| Flag | Description | Default |
|------|-------------|---------|
| `--storage-config <FILE>` | Storage configuration YAML (the shared global flag above). | in-memory |
| `--theme dark\|light\|mono` | Colour theme; `mono` is the ASCII/no-colour fallback. | `dark` |
| `--read-only` | Run the session as a read-only user; write controls render disabled, not hidden. | off |
| `--yes` | Skip mutation confirmation dialogs. | off |
| `--log-file <PATH>` | Write logs and audit lines to this file (created `0600`). Logs never go to the terminal while the UI owns it. | `./url_shortener-tui.log` |
| `--no-file-paths` | Refuse `link.import`/`link.export` `path` arguments; a `path` returns `unsupported`. Mutually exclusive with `--path-root`. | off |
| `--path-root <DIR>` | Confine `link.import`/`link.export` `path` to this absolute directory; a target outside the canonicalized root is refused `invalid_field`. Mutually exclusive with `--no-file-paths`. | off |

Exit codes: `0` on a clean quit; `1` for a parse error (an unknown flag, an
invalid `--theme`, a non-absolute `--path-root`, or combining `--no-file-paths`
with `--path-root`); `2` for a non-terminal stdin/stdout, a log-file open
failure, or `built without TUI support` when compiled with
`-DURLSHORTENER_ENABLE_TUI=OFF`; `4` on a storage/bootstrap failure.

**File-path capability.** As a local-operator tool the TUI enables
`link.import`/`link.export` `path` arguments against the operator's **full
filesystem** by default. At parity with `serve-stdio`, `--no-file-paths` turns
every `path` into `unsupported`, and `--path-root <DIR>` confines `path` to a
canonical root (component-wise prefix; symlinks resolved; a target outside it
is `invalid_field`). Both path forms require `write`, so a `--read-only`
session cannot use `path` at all (the registry denies it). The log/audit file
is created `0600` and the file sink refuses a pre-planted, group/other-readable,
hard-linked, or symlinked path (fail closed).

## Exit codes

| Code | Condition | Error code(s) |
|------|-----------|---------------|
| `0` | Success. | - |
| `1` | Link or job not found. | `not_found` |
| `2` | Bad input: invalid URL, slug, field, or command name; missing required arg; request not parseable; `dry_run`/`async` not supported for this command. | `invalid_envelope`, `unknown_command`, `invalid_field`, `invalid_url`, `invalid_slug`, `reserved_slug`, `unsupported` |
| `3` | Slug conflict: the requested slug is already in use. | `slug_conflict` |
| `4` | Storage or internal error; job queue full; job cancelled. | `storage_failure`, `internal`, `busy`, `job_cancelled` |
| `5` | Caller's role lacks the required permission. | `not_authorized` |

The exit-code mapping is implemented by `ExitCodeForAppError` in
`src/cli/link_command_dispatch.cpp` and by the commands error catalog
(`url_shortener::commands::all_error_infos()`). The full error table including
HTTP statuses is in
[`docs/commands/README.md`](../commands/README.md#1-envelope).

**Behavior difference between modes.** `link <verb>` parse errors (unrecognized
flag, missing required flag) exit `1` (legacy behavior, unchanged per contract
C4); diagnostics go to stderr. `cmd` argument and name errors exit `2`
(`invalid_envelope` or `invalid_field`) on stdout as a full response envelope;
usage errors (`cmd failed: invalid_request: ...`) go to stderr with exit `2`.

## Commands

Nine legacy `link <verb>` sugar commands are available. They compile flags to
the same `args` object as `cmd <name> --json` and produce the `result` field
only (backward-compatible with pre-0006 scripts). `link get` and `link preview`
each cover two REST endpoints via a `--slug`/`--id` selector. For the full
command catalog — including `link.list`, `db.*`, `job.*`, `system.info`,
`link.import`, and `link.export` (accessible via `cmd`) — see
[`docs/commands/README.md`](../commands/README.md).

| Verb | Command | REST operation_id | REST path |
|------|---------|-------------------|-----------|
| `link create` | `link.create` | [`post_api_v1_links`](../api/README.md) | `POST /api/v1/links` |
| `link get` | `link.get` | [`get_api_v1_links_slug`](../api/README.md) / [`get_api_v1_links_id_id`](../api/README.md) | `GET /api/v1/links/{slug}` / `GET /api/v1/links/id/{id}` |
| `link update` | `link.update` | [`patch_api_v1_links_slug`](../api/README.md) | `PATCH /api/v1/links/{slug}` |
| `link delete` | `link.delete` | [`delete_api_v1_links_slug`](../api/README.md) | `DELETE /api/v1/links/{slug}` |
| `link enable` | `link.enable` | [`post_api_v1_links_slug_enable`](../api/README.md) | `POST /api/v1/links/{slug}/enable` |
| `link disable` | `link.disable` | [`post_api_v1_links_slug_disable`](../api/README.md) | `POST /api/v1/links/{slug}/disable` |
| `link restore` | `link.restore` | [`post_api_v1_links_slug_restore`](../api/README.md) | `POST /api/v1/links/{slug}/restore` |
| `link preview` | `link.preview` | [`get_api_v1_links_slug_preview`](../api/README.md) | `GET /api/v1/links/{slug}/preview` |
| `link stats` | `link.stats` | [`get_api_v1_links_slug_stats`](../api/README.md) | `GET /api/v1/links/{slug}/stats` |

## Command pairs

Every `link <verb>` sugar command and its REST counterpart invoke the exact same
command-registry handler (see [ADR 0001](../adr/0001-cli-rest-shared-command-layer.md)
and [ADR 0002](../adr/0002-unified-command-model.md)), so the two are
functionally interchangeable for the nine original link commands. The `cmd`
column shows the equivalent generic invocation for every command including those
without `link <verb>` sugar. The commands below operate on the same link
(`docs`) so the pairs can be compared directly; full flag/field references are
in the sections below and in [`docs/api/README.md`](../api/README.md).

> **Note on gated routes.** The `GET /api/v1/links` (list), `GET /api/v1/jobs*`,
> and `POST /api/v1/commands` routes require `--command-api-enabled true` on the
> server. See [`docs/commands/README.md`](../commands/README.md#2-transports-how-each-carries-the-same-command)
> for access-control details.

| Command | REST | CLI |
|---|---|---|
| Create | `curl -X POST http://localhost:8000/api/v1/links -H 'Content-Type: application/json' -d '{"target_url":"https://example.com/docs","slug":"docs"}'` (`url` also accepted) | `url_shortener link create --url https://example.com/docs --slug docs` |
| Get by slug | `curl http://localhost:8000/api/v1/links/docs` | `url_shortener link get --slug docs` |
| Get by ID | `curl http://localhost:8000/api/v1/links/id/<id>` | `url_shortener link get --id <id>` |
| Update | `curl -X PATCH http://localhost:8000/api/v1/links/docs -H 'Content-Type: application/json' -d '{"enabled":false,"tags":["campaign"]}'` | `url_shortener link update --slug docs --enabled false --tags campaign` |
| Delete (soft) | `curl -X DELETE http://localhost:8000/api/v1/links/docs` | `url_shortener link delete --slug docs` |
| Enable | `curl -X POST http://localhost:8000/api/v1/links/docs/enable` | `url_shortener link enable --slug docs` |
| Disable | `curl -X POST http://localhost:8000/api/v1/links/docs/disable` | `url_shortener link disable --slug docs` |
| Restore | `curl -X POST http://localhost:8000/api/v1/links/docs/restore` | `url_shortener link restore --slug docs` |
| Preview | `curl http://localhost:8000/api/v1/links/docs/preview` | `url_shortener link preview --slug docs` |
| Stats | `curl 'http://localhost:8000/api/v1/links/docs/stats?from=1710000000&to=1710086400&bucket=day'` | `url_shortener link stats --slug docs --from 1710000000 --to 1710086400 --bucket day` |
| List (active) | `curl 'http://localhost:8000/api/v1/links?status=active'` (gated) | `url_shortener cmd link.list --arg status=active` |
| Import (inline) | `curl -X POST http://localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"link.import","args":{"format":"csv","data":"https://example.com/a,slug1"}}'` (gated) | `url_shortener cmd link.import --json '{"format":"csv","data":"https://example.com/a,slug1"}'` |
| Export (inline) | `curl -X POST http://localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"link.export","args":{"format":"csv"}}'` (gated) | `url_shortener cmd link.export --arg format=csv` |
| Get job | `curl http://localhost:8000/api/v1/jobs/<id>` (gated) | `url_shortener cmd job.get --arg id=<id>` |
| List jobs | `curl http://localhost:8000/api/v1/jobs` (gated) | `url_shortener cmd job.list` |
| Cancel job | `curl -X POST http://localhost:8000/api/v1/jobs/<id>/cancel` (gated) | `url_shortener cmd job.cancel --arg id=<id>` |
| DB tables | `curl -X POST http://localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"db.tables"}'` (gated) | `url_shortener cmd db.tables` |
| DB rows | `curl -X POST http://localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"db.rows","args":{"table":"links"}}'` (gated) | `url_shortener cmd db.rows --arg table=links` |
| System info | `curl -X POST http://localhost:8000/api/v1/commands -H 'Content-Type: application/json' -d '{"v":1,"command":"system.info"}'` (gated) | `url_shortener cmd system.info` |

**One real difference:** REST commands act on whatever link state the running
server holds in its configured backend. The CLI's default in-memory store is
per-invocation and does not persist across separate process runs (see
[Storage and backend selection](#storage-and-backend-selection) above). To
share the same persistent backend as the server, supply `--storage-config
<yaml>` (or set `URL_SHORTENER_STORAGE_CONFIG`) pointing at the same config
file. Within a single running server, the REST pairs above work exactly as
shown regardless of storage backend.

## Jobs from the CLI

`link.import` and `link.export` are job-kind commands: `cmd` submits the job
and, by default, waits for it to finish (polling `job.get` internally) and then
prints the final job envelope:

```json
{"v":1,"id":"cli-1","ok":true,"result":{"id":"j-1","command":"link.import",
"state":"succeeded","result":{"total":1,"created":1,...},...}}
```

Pass `--no-wait` to print the accepted (queued) envelope immediately without
waiting. This is only useful against a long-lived process (a running server or
`serve-stdio` session in a background shell); in a one-shot `cmd` invocation the
process exits before the job can run.

**Polling `job.get` manually:**

```bash
# Submit without waiting
url_shortener cmd link.import \
  --json '{"format":"csv","data":"https://example.com/a,slug1"}' \
  --no-wait
# Returns: {"v":1,"id":"cli-1","ok":true,"job":{"id":"j-1","state":"queued"}}

# Poll until done (replace j-1 with the actual id)
url_shortener cmd job.get --arg id=j-1

# Cancel if needed
url_shortener cmd job.cancel --arg id=j-1
```

Job records are held in memory by the process that owns the registry. They are
lost on restart; `job.list` shows only the current process's jobs.

## Security notes

- **Local operator rights.** All local modes (`link <verb>`, `cmd`, `serve-stdio`,
  `tui`) run as `ControlSet::Admin` by default, the same trust boundary as the
  person who can start the process. Pass `--read-only` to any mode to downgrade
  to `ControlSet::User` (read-only commands only).
- **`path` arguments allowed locally, refused on REST.** `link.import`/`link.export`
  `path` arguments are granted to local operators by default; REST always refuses
  them with `unsupported`. Pass `--no-file-paths` to disable, or `--path-root <DIR>`
  to confine to a directory.
- **No listener in local modes.** `cmd`, `serve-stdio`, and `tui` never start an
  HTTP/HTTPS server, never bind a socket, and never touch `uri.txt`. The process
  communicates only via stdin/stdout (stdio) or the terminal (tui).
- **No secrets in `cmd` output.** Logging is disabled by default in `cmd` so
  stderr stays clean. Pass `--log-file <PATH>` to write an audit trail to a file,
  or `--verbose` to write to stderr. Neither path logs request argument values
  (audit lines record command name, `id`, and outcome only).

---

### `link create`

Create a short link from a target URL.

**REST counterpart:** [`post_api_v1_links`](../api/README.md)
(`POST /api/v1/links`)

**Syntax**

```
url_shortener link create --url <URL> [options]
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--url <URL>` | yes | Target URL to shorten. |
| `--slug <SLUG>` | no | Custom slug; auto-generated if omitted. |
| `--redirect-type <TYPE>` | no | `temporary` (default) or `permanent`. |
| `--expires-at <RFC3339>` | no | Expiry timestamp in RFC3339 UTC format. |
| `--enabled <BOOL>` | no | Initial enabled state (`true`/`false`/`1`/`0`/`on`/`off`/`yes`/`no`). Default `true`. |
| `--tag <TAG>` | no | Tag to attach; repeatable (`--tag a --tag b`). |
| `--metadata <KEY=VALUE>` | no | Metadata entry; repeatable (`--metadata k=v --metadata x=y`). |
| `--campaign-name <VALUE>` | no | Campaign name. |
| `--campaign-source <VALUE>` | no | Campaign source. |
| `--campaign-medium <VALUE>` | no | Campaign medium. |
| `--campaign-term <VALUE>` | no | Campaign term. |
| `--campaign-content <VALUE>` | no | Campaign content. |
| `--campaign-id <VALUE>` | no | Campaign identifier. |
| `--base-domain <URL>` | no | Base domain for rendering the generated short URL (overrides `SHORTENER_BASE_DOMAIN`). |
| `--allow-private-targets` | no | Permit private/intranet target URLs for this invocation. |

**Example — success**

```bash
url_shortener link create --url https://example.com/docs --slug docs
```

stdout (single JSON line, formatted here for readability):

```json
{
  "id": "a1b2c3d4",
  "slug": "docs",
  "url": "https://example.com/docs",
  "short_url": "http://localhost:8000/docs",
  "created_at": "2026-09-23T10:00:00Z",
  "updated_at": "2026-09-23T10:00:00Z",
  "status": "active",
  "redirect_type": "temporary",
  "tags": [],
  "metadata": {},
  "campaign": null,
  "stats": {
    "total_redirects": 0,
    "redirects_24h": 0,
    "redirects_7d": 0,
    "last_accessed_at": null
  }
}
```

**Example — error** (invalid URL)

stderr: `create failed: invalid_url` — exit code 2.

---

### `link get`

Fetch a link's full metadata by slug or ID. Covers two REST endpoints through a
single verb; exactly one selector must be supplied.

**REST counterparts:**
- by slug: [`get_api_v1_links_slug`](../api/README.md) (`GET /api/v1/links/{slug}`)
- by ID: [`get_api_v1_links_id_id`](../api/README.md) (`GET /api/v1/links/id/{id}`)

**Syntax**

```
url_shortener link get --slug <SLUG>
url_shortener link get --id <ID>
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | one of | Look up by slug. |
| `--id <ID>` | one of | Look up by opaque ID. |

**Example — success**

```bash
url_shortener link get --slug docs
```

stdout: same `LinkView` JSON shape as `link create` above.

**Example — error** (not found)

stderr: `get failed: not_found` — exit code 1.

---

### `link update`

Partially update a link's mutable fields (PATCH semantics: only supplied flags
are applied; omitted flags leave their fields unchanged).

**REST counterpart:** [`patch_api_v1_links_slug`](../api/README.md)
(`PATCH /api/v1/links/{slug}`)

**Syntax**

```
url_shortener link update --slug <SLUG> [options]
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug of the link to update. |
| `--enabled <BOOL>` | no | New enabled state. |
| `--expires-at <RFC3339\|clear>` | no | New expiry (RFC3339 UTC), or the literal `clear` to remove the expiry. |
| `--tags <A,B,C>` | no | Replacement tag list as a comma-separated value; `--tags ""` clears all tags. |
| `--metadata <KEY=VALUE,...>` | no | Replacement metadata as comma-separated KEY=VALUE pairs; `--metadata ""` clears all entries. |
| `--campaign-name <VALUE>` | no | Campaign name. |
| `--campaign-source <VALUE>` | no | Campaign source. |
| `--campaign-medium <VALUE>` | no | Campaign medium. |
| `--campaign-term <VALUE>` | no | Campaign term. |
| `--campaign-content <VALUE>` | no | Campaign content. |
| `--campaign-id <VALUE>` | no | Campaign identifier. |
| `--clear-campaign` | no | Clear the campaign field (explicit null). Mutually exclusive with `--campaign-*`. |

**Example — success**

```bash
url_shortener link update --slug docs --enabled false --tags "campaign,email"
```

stdout: updated `LinkView` JSON.

**Example — error** (not found)

stderr: `update failed: not_found` — exit code 1.

---

### `link delete`

Soft-delete a link by slug (sets `deleted_at`; the record is retained and can be
restored with `link restore`).

**REST counterpart:** [`delete_api_v1_links_slug`](../api/README.md)
(`DELETE /api/v1/links/{slug}`)

**Syntax**

```
url_shortener link delete --slug <SLUG>
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug of the link to soft-delete. |

**Example — success**

```bash
url_shortener link delete --slug docs
```

stdout: `LinkView` JSON with `"status": "deleted"`.

**Example — error** (not found)

stderr: `delete failed: not_found` — exit code 1.

---

### `link enable`

Enable a previously disabled link.

**REST counterpart:** [`post_api_v1_links_slug_enable`](../api/README.md)
(`POST /api/v1/links/{slug}/enable`)

**Syntax**

```
url_shortener link enable --slug <SLUG>
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug of the link to enable. |

**Example — success**

```bash
url_shortener link enable --slug docs
```

stdout: `LinkView` JSON with `"status": "active"`.

**Example — error** (not found)

stderr: `enable failed: not_found` — exit code 1.

---

### `link disable`

Disable a link so it stops redirecting (sets `enabled` to false without deleting
the record).

**REST counterpart:** [`post_api_v1_links_slug_disable`](../api/README.md)
(`POST /api/v1/links/{slug}/disable`)

**Syntax**

```
url_shortener link disable --slug <SLUG>
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug of the link to disable. |

**Example — success**

```bash
url_shortener link disable --slug docs
```

stdout: `LinkView` JSON with `"status": "disabled"`.

**Example — error** (not found)

stderr: `disable failed: not_found` — exit code 1.

---

### `link restore`

Restore a soft-deleted link (clears `deleted_at`).

**REST counterpart:** [`post_api_v1_links_slug_restore`](../api/README.md)
(`POST /api/v1/links/{slug}/restore`)

**Syntax**

```
url_shortener link restore --slug <SLUG>
```

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug of the link to restore. |

**Example — success**

```bash
url_shortener link restore --slug docs
```

stdout: `LinkView` JSON with `"status": "active"` and `deleted_at` cleared.

**Example — error** (not found)

stderr: `restore failed: not_found` — exit code 1.

---

### `link preview`

Preview a link's resolved status without triggering a redirect. Returns the
reduced preview projection shared verbatim with the REST
`GET /api/v1/links/{slug}/preview` endpoint — an operator's safety check that
surfaces exactly the fields needed to decide whether a link is live, expired, or
soft-deleted, and nothing else. The JSON object carries, in order: `slug`,
`url`, `status`, `redirect_type`, `enabled`, `expires_at`, `deleted_at`
(`expires_at`/`deleted_at` are `null` when unset). It deliberately omits the
`id`, `short_url`, timestamps, tags, metadata, campaign, and stats that
`link get` returns.

**REST counterpart:** [`get_api_v1_links_slug_preview`](../api/README.md)
(`GET /api/v1/links/{slug}/preview`)

**Syntax**

```
url_shortener link preview --slug <SLUG>
url_shortener link preview --id <ID>
```

Exactly one of `--slug` or `--id` must be supplied (same selector rule as
`link get`).

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | one of | Preview by slug. |
| `--id <ID>` | one of | Preview by opaque ID. |

**Example — success**

```bash
url_shortener link preview --slug docs
```

stdout: reduced preview JSON, e.g.
`{"slug":"docs","url":"https://example.com/docs","status":"active","redirect_type":"temporary","enabled":true,"expires_at":null,"deleted_at":null}`.

**Example — error** (not found)

stderr: `preview failed: not_found` — exit code 1.

---

### `link stats`

Retrieve aggregate click statistics for a link over a time window.

**REST counterpart:** [`get_api_v1_links_slug_stats`](../api/README.md)
(`GET /api/v1/links/{slug}/stats`)

**Syntax**

```
url_shortener link stats --slug <SLUG> --from <EPOCH> --to <EPOCH> --bucket <BUCKET>
```

All four flags are required.

**Flags**

| Flag | Required | Description |
|------|----------|-------------|
| `--slug <SLUG>` | yes | Slug to report on. |
| `--from <EPOCH>` | yes | Window start as a Unix epoch. |
| `--to <EPOCH>` | yes | Window end as a Unix epoch. |
| `--bucket <BUCKET>` | yes | Aggregation granularity: `hour`, `day`, or `week`. |

**Example — success**

```bash
url_shortener link stats \
  --slug docs \
  --from 1695000000 \
  --to 1695086400 \
  --bucket hour
```

stdout (single JSON line, formatted here for readability):

```json
{
  "slug": "docs",
  "total_attempts": 42,
  "successful_redirects": 38,
  "attempts_by_status_code": {"302": 38},
  "attempts_by_domain": {"example.com": 12},
  "time_buckets": [{"bucket_start": 1695000000, "count": 38}],
  "from": 1695000000,
  "to": 1695086400,
  "bucket": "hour"
}
```

**Example — error** (not found)

stderr: `stats failed: not_found` — exit code 1.
