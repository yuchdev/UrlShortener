# Test-gap risk-ranking rubric

Expands the P0/P1/P2 tiers in `SKILL.md` with URL-Shortener-specific criteria and
concrete module examples, so the `testing-expert` agent ranks uncovered code
consistently across runs. The ranking question is always: *if this uncovered
branch is wrong, what is the blast radius?*

## P0 — must test (safety- or correctness-critical)

An untested defect here serves a wrong/revoked redirect, exposes the service to
SSRF, corrupts a link's lifecycle, or leaks a secret. Rank first.

- **Redirect resolution** (core resolver, request handler fast path): the hot path
  that turns a slug into a `Location`. A bug serves the wrong target, a 200 where a
  redirect is due, or a crash on the busiest route.
- **Link-lifecycle precedence** (`active`/`disabled`/`expired`/`deleted`, with
  precedence `deleted > disabled > expired > active`): a wrong branch resurrects a
  revoked link or 404s a live one. Test each state and each precedence conflict.
- **Cache-aside invalidation** (lookup → store → cache fill; invalidation on
  update/delete): stale cache after a disable/delete keeps serving a dead link.
  Test that a mutation invalidates before the next read.
- **SSRF / `isPrivateHost`** (`shortener_allow_private_targets` gate): private,
  loopback, link-local, and metadata targets must stay blocked when the flag is
  off; test IPv6 and encoded-host bypasses.
- **Auth on management routes**: create/disable/delete must reject unauthenticated
  callers; test the negative path, not just the happy path.
- **Parameterized-SQL boundaries** (SOCI binds): a slug/URL with quotes/semicolons
  must not alter a query. Test hostile input through the storage layer.
- **Secret redaction** (`observability::redactSecretValue` and log sinks): DSNs,
  TLS keys, tokens, and the analytics salt must never reach a log line.

## P1 — should test (behavioural correctness)

Wrong output but bounded blast radius; caught downstream or degrades gracefully.

- Storage backend strategy implementations and the `StorageFactory`
  (inmemory/sqlite/postgres/redis) — each backend honouring the same `IStorage`
  contract.
- Config parsing / `ServerConfig` from YAML (defaults, overrides, invalid values).
- Analytics recording — must be **best-effort**: an analytics failure must never
  break or slow a redirect. Test that the redirect still succeeds when the
  analytics sink errors.
- Struct/record serialisation round-trips and `std::optional` field defaults.

## P2 — nice to have (low-risk / cosmetic)

- CLI/`program_options` formatting and help text.
- Log message shape (beyond the redaction guarantee, which is P0).
- Metrics label spelling / observability niceties.

## Ranking procedure

1. Map each uncovered line/branch (from the lcov `coverage` report or CTest
   inventory) to a behaviour, not just a line number.
2. Ask the blast-radius question; assign the highest tier that applies.
3. Within a tier, order by likelihood × reachability (the redirect hot path beats a
   rare error branch).
4. For each P0, name a concrete suggested test (input → expected outcome), so the
   list is directly actionable by `testing-expert`.

## Anti-patterns

- Ranking by coverage-percentage delta instead of risk (a 2% gain over the redirect
  resolver beats a 20% gain over CLI formatting).
- Listing "add tests for module X" without the specific untested behaviour.
- Treating an SSRF, auth, or redaction branch as P1 because it "looks simple".
