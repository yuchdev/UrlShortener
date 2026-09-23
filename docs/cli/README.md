# URL Shortener CLI Reference

The `url_shortener` binary exposes link-management commands through a `link <verb>`
subcommand syntax, using the same `LinkCommandService` layer as the REST API. Every
verb maps one-to-one to a REST operation; see [`docs/api/README.md`](../api/README.md)
for the full REST inventory and field-by-field response documentation (this reference
does not duplicate those descriptions).

## Storage limitation

The CLI uses the in-memory link store (`linkRepository()`) which is a per-process,
non-persistent singleton. **Two separate CLI invocations do not share state.** A link
created by `link create` in one process invocation is invisible to `link get` in a
separate invocation; only calls within the same process observe each other. Practical
consequence: the CLI cannot be used for create-then-read scripting across separate
process invocations with the current in-memory backend. See
[Milestone 0003 status, Task 03.0](../roadmap/0003-cli_rest_interfaces/status.md)
for full background on this design decision.

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
