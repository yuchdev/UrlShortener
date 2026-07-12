# Secret-scan pattern catalog

Documents the exact detection engine shared by the `/secret-scan` skill and the
`PreToolUse(Write|Edit|MultiEdit)` hook (`.github/hooks/scripts/secret_scan.py`). Both use
this same table, so the manual sweep and the automatic block agree. When the hook
source changes, update this file (drift here means the catalog lies about what is
actually blocked).

> Note: this file deliberately describes secret *shapes* in prose and uses
> `${VAR}`/placeholder forms in every example — a literal credential here would be
> blocked by the very hook it documents.

## Scope

This is a C++ URL shortener; it integrates **no** AI providers or cloud SDKs, so
the reference project's AWS/Anthropic/OpenAI/Google/Slack key patterns are
intentionally omitted. The catalog targets this service's real secret surface:
TLS private keys, database/cache DSNs, the analytics salt, tokens, and generic
assigned secrets.

## Detected shapes → per-type remediation

| Type | Shape (described, not literal) | Remediation |
|------|--------------------------------|-------------|
| Private key block | a `BEGIN … PRIVATE KEY` PEM header (RSA/EC/DSA/OPENSSH/PGP) — the `--tls-key` material | Remove; rotate the key/cert; store it outside the repo and pass its path via `ServerConfig`/`--tls-key`, never inline. |
| Postgres URL with password | a `postgres://`/`postgresql://` DSN carrying inline `user:password@` credentials | Move credentials to env; build the DSN as `postgres://user:${PGPASSWORD}@host` and load via YAML/`ServerConfig`. |
| Redis URL with password | a `redis://`/`rediss://` URL carrying an inline password before `@` | Move the password to `${REDIS_PASSWORD}`; keep it out of source and config committed to the repo. |
| Analytics hash salt | an `analytics_..._hash_salt` / `url_shortener_analytics_hash_salt` assignment to a quoted 6+ char literal | Load the salt from env/secret manager at runtime; never commit the production salt. The `dev-analytics-salt` default is allowlisted as a non-secret dev value. |
| GitHub token | prefixes `ghp_`/`gho_`/`ghu_`/`ghs_`/`ghr_` then 36+ chars | Revoke the PAT; use `${GITHUB_TOKEN}` (the repo uses the `gh` CLI / GitHub MCP). |
| Generic assigned secret | `password`/`passwd`/`secret`/`token`/`api_key`/`passphrase` assigned a quoted 8+ char literal | Replace the literal with an env/`${VAR}` reference resolved through `ServerConfig`/YAML. |

## Placeholder allowlist (why a line is *not* flagged)

A line is skipped if it contains any of these case-insensitive substrings:
`example`, `placeholder`, `your-`, `your_`, `changeme`, `dummy`, `xxxx`, `${`,
`$env:`, `<your`, `redacted`, `fake`, `test`, `dev-analytics-salt`.

This is why `dsn = "postgres://app:${PGPASSWORD}@db/prod"` and
`token = "your-token-here"` pass — they read as obvious non-secrets. Consequence:
the correct fix for a real hit is to convert it into an allowlisted form (a
`${VAR}` reference), which also makes the scanner pass.

## Skipped file types

Binary/large artifacts are not scanned: `.lock`, `.png`, `.jpg`, `.jpeg`, `.gif`,
`.pdf`, `.db`, `.sqlite`. A committed `.sqlite`/`.db` is skipped by the scanner but
may still contain user data — never commit a populated database regardless.

## Reporting rules

- Print `file:line: <type>: <short excerpt>` — **never** the full secret value.
- On any hit: instruct the user to (1) remove the literal, (2) **rotate** it if it
  ever reached a remote, (3) replace with an env/`${VAR}` reference loaded through
  `ServerConfig`/YAML at runtime, or a secrets manager.
- If the secret was already committed, recommend `git filter-repo` / history
  rewrite — deletion in a new commit does not remove it from history.

## False-positive handling

If a real detection is genuinely not a secret (e.g. a test fixture), prefer making
it *look* like a placeholder (add `example`/`dummy`/`test` context or a `${VAR}`)
rather than widening the allowlist — the allowlist is shared with the blocking
hook, so loosening it weakens the automatic gate for everyone.
