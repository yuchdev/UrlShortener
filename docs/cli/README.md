# URL Shortener CLI Reference

The `url_shortener` binary exposes link-management commands through a `link <verb>`
subcommand syntax, using the same `LinkCommandService` layer as the REST API. Every
verb maps one-to-one to a REST operation; see [`docs/api/README.md`](../api/README.md)
for the full REST inventory and field-by-field response documentation (this reference
does not duplicate those descriptions).

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

```
url_shortener link <verb> [flags...]
```

On success (exit 0), a single JSON object is written to **stdout**. On failure
(exit > 0), a diagnostic line is written to **stderr** with stdout empty — suitable
for scripting with `$?` alone:

```bash
url_shortener link create --url https://example.com/docs --slug docs | jq '.slug'
```

## Exit codes

| Code | Condition |
|------|-----------|
| `0` | Success. |
| `1` | Link not found. |
| `2` | Bad input: invalid URL, slug, or field value; or a reserved slug was supplied. |
| `3` | Slug conflict: the requested slug is already in use. |
| `4` | Storage or internal error. |

The mapping is implemented by `ExitCodeForAppError` in
`src/cli/link_command_dispatch.cpp`.

Parse errors (an unrecognized flag, a malformed flag value, or a missing
required flag) also exit `1`, thrown before any command reaches
`LinkCommandService` - diagnostics appear on stderr, same as any other
failure.

## Commands

Nine verbs are available. `link get` is the only verb that covers two REST
endpoints (`GET /api/v1/links/{slug}` and `GET /api/v1/links/id/{id}`) via a
single verb with a `--slug`/`--id` selector. `link preview` maps to a single
REST endpoint (`GET /api/v1/links/{slug}/preview`) but accepts the same
`--slug`/`--id` selector as a CLI-only convenience - there is no
by-id REST preview endpoint.

| Verb | REST operation_id | REST path |
|------|-------------------|-----------|
| `link create` | [`post_api_v1_links`](../api/README.md) | `POST /api/v1/links` |
| `link get` | [`get_api_v1_links_slug`](../api/README.md) / [`get_api_v1_links_id_id`](../api/README.md) | `GET /api/v1/links/{slug}` / `GET /api/v1/links/id/{id}` |
| `link update` | [`patch_api_v1_links_slug`](../api/README.md) | `PATCH /api/v1/links/{slug}` |
| `link delete` | [`delete_api_v1_links_slug`](../api/README.md) | `DELETE /api/v1/links/{slug}` |
| `link enable` | [`post_api_v1_links_slug_enable`](../api/README.md) | `POST /api/v1/links/{slug}/enable` |
| `link disable` | [`post_api_v1_links_slug_disable`](../api/README.md) | `POST /api/v1/links/{slug}/disable` |
| `link restore` | [`post_api_v1_links_slug_restore`](../api/README.md) | `POST /api/v1/links/{slug}/restore` |
| `link preview` | [`get_api_v1_links_slug_preview`](../api/README.md) | `GET /api/v1/links/{slug}/preview` |
| `link stats` | [`get_api_v1_links_slug_stats`](../api/README.md) | `GET /api/v1/links/{slug}/stats` |

## Command pairs

Every CLI verb and its REST counterpart invoke the exact same
`LinkCommandService` method (see [ADR 0001](../adr/0001-cli-rest-shared-command-layer.md)),
so the two are functionally interchangeable - pick whichever transport fits the
context. The commands below operate on the same link (`docs`) so the pairs can
be compared directly; full flag/field references are in the sections below and
in [`docs/api/README.md`](../api/README.md).

| Command | REST | CLI |
|---|---|---|
| Create | `curl -X POST http://localhost:8000/api/v1/links -H 'Content-Type: application/json' -d '{"url":"https://example.com/docs","slug":"docs"}'` | `url_shortener link create --url https://example.com/docs --slug docs` |
| Get by slug | `curl http://localhost:8000/api/v1/links/docs` | `url_shortener link get --slug docs` |
| Get by ID | `curl http://localhost:8000/api/v1/links/id/<id>` | `url_shortener link get --id <id>` |
| Update | `curl -X PATCH http://localhost:8000/api/v1/links/docs -H 'Content-Type: application/json' -d '{"enabled":false,"tags":["campaign"]}'` | `url_shortener link update --slug docs --enabled false --tags campaign` |
| Delete (soft) | `curl -X DELETE http://localhost:8000/api/v1/links/docs` | `url_shortener link delete --slug docs` |
| Enable | `curl -X POST http://localhost:8000/api/v1/links/docs/enable` | `url_shortener link enable --slug docs` |
| Disable | `curl -X POST http://localhost:8000/api/v1/links/docs/disable` | `url_shortener link disable --slug docs` |
| Restore | `curl -X POST http://localhost:8000/api/v1/links/docs/restore` | `url_shortener link restore --slug docs` |
| Preview | `curl http://localhost:8000/api/v1/links/docs/preview` | `url_shortener link preview --slug docs` |
| Stats | `curl http://localhost:8000/api/v1/links/docs/stats?from=1710000000&to=1710086400&bucket=day` | `url_shortener link stats --slug docs --from 1710000000 --to 1710086400 --bucket day` |

**One real difference:** REST commands act on whatever link state the running
server holds in its configured backend. The CLI's default in-memory store is
per-invocation and does not persist across separate process runs (see
[Storage and backend selection](#storage-and-backend-selection) above). To
share the same persistent backend as the server, supply `--storage-config
<yaml>` (or set `URL_SHORTENER_STORAGE_CONFIG`) pointing at the same config
file. Within a single running server, the REST pairs above work exactly as
shown regardless of storage backend.

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
