# QA Manual Test Checklist – CLI Link Commands

**Feature:** `link` one-shot CLI subcommands (nine verbs: create / get /
update / delete / enable / disable / restore / preview / stats)
**Stage:** CLI command layer (`app::LinkCommandService` + `main.cpp` dispatch)
**Last updated:** 2026-09-23

---

## Environment

| Item | Value |
|------|-------|
| Built binary | `cmake-build/url_shortener` (or `cmake-build/Debug/url_shortener`) |
| Working directory | Any writable directory; no temp-dir isolation is needed for state between commands (see Storage below) |
| No network needed | All tests use `https://example.com` or similar; no live external service required |
| No server needed | CLI mode must NOT require a running server and must NOT bind a listening socket |

---

## Storage scoping — read this before running multi-step cases

CLI mode **never** touches `uri.txt`. `uri.txt` is `UriMapSingleton`'s legacy
key-value store for the generic Fallback routes (`GET`/`POST`/`DELETE /{path}`,
`/`); link records live in `linkRepository()`, a separate process-local
in-memory singleton. `main.cpp` dispatches to CLI mode before the `uri.txt`
load, and `DispatchLinkCommand` never calls `UriMapSingleton::getInstance()`.

`linkRepository()` (`src/storage/link_repository.cpp`) is a function-local
`static InMemoryMetadataRepository`. Its lifetime is the process. **Two
separate CLI process invocations do NOT share state**: a `link create` in one
invocation is invisible to a `link get` in a second, separately-launched
process. Each CLI invocation begins with an empty link store.

In practice this means the CLI is **not suitable** for create-then-read
scripting across separate process invocations with the current in-memory
backend. Within a single process invocation the store is consistent (e.g. a
unit test that calls create and then get within the same process observes both
records), but that is not observable from separate shell invocations. This is
a documented limitation, not a bug: reconciling the two storage paths is out
of scope for this milestone (see
[`plan.md`](/docs/roadmap/0003-cli_rest_interfaces/plan.md)).

---

## Implemented behavior

All nine `link <verb>` subcommands are fully implemented and dispatched:

- `ParseResult::command` carries an optional `LinkCliCommand` variant covering
  all nine verbs.
- `main.cpp` branches to `cli::DispatchLinkCommand` before constructing
  `HttpServer` or `net::io_context`, so no listener is ever bound.
- On success every verb writes a single JSON object to stdout (using the same
  serializers as the REST handlers) and exits `0`.
- On failure stdout is empty; a diagnostic is written to stderr; the exit code
  encodes the error category:

| Exit code | `AppErrorCode` |
|-----------|----------------|
| `0` | `none` (success) |
| `1` | `not_found` |
| `2` | `invalid_url`, `invalid_slug`, `invalid_field`, `reserved_slug` |
| `3` | `slug_conflict` |
| `4` | `storage_failure`, `internal` |

CLI parse errors (missing required flags, unrecognized flags, bad flag values)
exit `1` via `main.cpp`'s top-level `catch` block, before dispatch is reached.

---

## Test cases

### 1. `link create` – happy path

```bash
./url_shortener link create \
    --url https://example.com/landing \
    --base-domain http://sho.rt
```

**Expected:**

- Exit code: `0`
- Stdout: single-line JSON
  - `id` field: non-empty string
  - `slug` field: non-empty 7-character alphanumeric string
  - `url` field: `"https://example.com/landing"`
  - `status` field: `"active"`
  - `short_url` field starts with `"http://sho.rt/"`
- No server port bound (`ss -tln | grep :8000` stays empty)

---

### 2. `link create` – explicit slug

```bash
./url_shortener link create \
    --url https://example.com/promo \
    --slug summer25
```

**Expected:**

- Exit code: `0`
- JSON `slug == "summer25"`
- No `uri.txt` created

---

### 3. `link create` then `link get` – single-process state

Because two separate CLI processes do not share state (see Storage above), the
create-then-get round trip is observable only within the same process. The
integration test `dispatch_create_then_get_roundtrip_same_process` (unit,
`cli__02_cli_dispatch_reachable`) confirms this in-process. The cross-process
shell sequence below is expected to return `not_found` (exit 1) on the `get`:

```bash
./url_shortener link create --url https://persist.example.com --slug persist-test
./url_shortener link get --slug persist-test  # exits 1 – empty store, not a bug
```

Cross-process create-then-read is intentionally untestable with the current
in-memory backend.

---

### 4. `link get` by slug

```bash
./url_shortener link get --slug summer25
```

**Expected (when run in the same process that created the slug):**

- Exit code: `0`
- Stdout JSON: `slug == "summer25"`, `url` matches the created URL

**Expected (when run in a fresh process with an empty store):**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic mentioning "not found"

---

### 5. `link get` by id

```bash
./url_shortener link get --id <uuid>
```

**Expected (when the id exists):**

- Exit code: `0`
- Stdout JSON: `id == <uuid>`

**Expected (when the id is unknown):**

- Exit code: `1`, stdout empty, stderr diagnostic

---

### 6. `link get` – not found

```bash
./url_shortener link get --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic indicating "not found"

---

### 7. `link create` – invalid URL (SSRF guard)

```bash
./url_shortener link create --url "not-a-url"
./url_shortener link create --url "ftp://files.example.com"
./url_shortener link create --url "javascript:alert(1)"
./url_shortener link create --url "http://10.0.0.1/internal"
```

**Expected (each):**

- Exit code: `2` (invalid_url)
- Stdout: empty
- No `uri.txt` created or modified (this file is never touched by CLI)

---

### 8. `link create` – reserved slug

```bash
./url_shortener link create --url https://example.com --slug health
./url_shortener link create --url https://example.com --slug api
./url_shortener link create --url https://example.com --slug ADMIN
```

**Expected (each):**

- Exit code: `2` (reserved_slug)
- Stderr: error message mentions "reserved" or the error code

---

### 9. `link create` – duplicate slug conflict

Because the in-memory store is per-process, a conflict is observable only when
both creates happen in the same process (e.g., via the REST server or in-process
test). From separate CLI invocations the second `create` sees an empty store and
exits `0`. The REST path and the `slug_conflict` (exit 3) contract are covered
by the characterization tests in `tests/unit/http/10_link_handlers.cpp`.

---

### 10. `link update` – not found

```bash
./url_shortener link update --slug doesnotexist --enabled false
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic indicating "not found"

(The happy path requires a pre-existing slug; cross-process seeding is not
possible with the in-memory backend. See integration test
`tests/integration/cli/11_link_update_not_found_and_invalid_input.py`.)

---

### 11. `link update` – invalid `--enabled` value

```bash
./url_shortener link update --slug some-slug --enabled notabool
```

**Expected:**

- Exit code: `1` (parse error)
- Stdout: empty

---

### 12. `link update` – missing required flag

```bash
./url_shortener link update --enabled false   # missing --slug
```

**Expected:**

- Exit code: `1` (parse error)

---

### 13. `link delete` – not found

```bash
./url_shortener link delete --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic indicating "not found"

---

### 14. `link delete` – empty slug rejected

```bash
./url_shortener link delete --slug ""
```

**Expected:**

- Exit code: `1`

---

### 15. `link enable` – not found

```bash
./url_shortener link enable --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic

---

### 16. `link enable` – unknown flag rejected

```bash
./url_shortener link enable --slug some-slug --bogus-flag x
```

**Expected:**

- Exit code: `1`

---

### 17. `link disable` – not found

```bash
./url_shortener link disable --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic

---

### 18. `link restore` – not found

```bash
./url_shortener link restore --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic

---

### 19. `link preview` – not found by slug

```bash
./url_shortener link preview --slug doesnotexist
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic

---

### 20. `link preview` – not found by id

```bash
./url_shortener link preview --id ffffffff-0000-0000-0000-000000000000
```

**Expected:**

- Exit code: `1`
- Stdout: empty
- Stderr: diagnostic

---

### 21. `link preview` – both selectors rejected

```bash
./url_shortener link preview --slug a-slug --id ffffffff-0000-0000-0000-000000000000
```

**Expected:**

- Exit code: `1` (ambiguous selector)

---

### 22. `link preview` – no selector rejected

```bash
./url_shortener link preview
```

**Expected:**

- Exit code: `1`

---

### 23. `link stats` – invalid window

```bash
# from > to
./url_shortener link stats --slug any --from 1800000000 --to 1700000000 --bucket day
# unsupported bucket
./url_shortener link stats --slug any --from 1700000000 --to 1800000000 --bucket minute
```

**Expected (each):**

- Exit code: `2` (invalid_field)
- Stderr: error message indicates the invalid field

---

### 24. CLI does not start server

```bash
# Pre-check: confirm port 8000 is free
ss -tln | grep :8000   # should be empty

./url_shortener link create --url https://example.com

# Immediately after exit:
ss -tln | grep :8000   # must still be empty
```

**Expected:**

- Port 8000 is NOT open after the CLI command exits
- Process exits within 2 seconds

Repeat for `link update --slug x --enabled false`, `link delete --slug x`,
`link enable --slug x`, `link disable --slug x`, `link restore --slug x`, and
`link preview --slug x`. All nine verbs must satisfy this guarantee.

---

### 25. `--help` – server mode still works

```bash
./url_shortener --help
```

**Expected:**

- Exit code: `0`
- Server-mode usage printed (lists server flags, not `link` verbs)
- No server started

---

### 26. Server mode still works after CLI additions

```bash
./url_shortener --http-port 28999 --tls-enabled false &
SERVER_PID=$!
sleep 1.5
curl -fsS http://localhost:28999/healthz
kill $SERVER_PID
```

**Expected:**

- `/healthz` returns HTTP 200 (server mode unaffected by CLI additions)

---

## Auth paths

CLI commands do not traverse `AccessGuard` or `auth_audit_log` in this
implementation (parity with the existing REST posture; tracked as a milestone
follow-up). If auth-gated CLI commands are added in a later stage, verify:

- Unauthenticated invocation is rejected with a clear error message and a
  non-zero exit code.
- Token/credential arguments are accepted and validated before dispatch.

---

## Artifacts on failure

```bash
./url_shortener link create ... 2>fail.stderr
# uri.txt is never created by CLI mode; checking for it is not meaningful
```
