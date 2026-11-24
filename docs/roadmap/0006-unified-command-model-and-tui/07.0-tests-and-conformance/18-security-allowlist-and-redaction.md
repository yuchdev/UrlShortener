# 18 - Security: allowlist and redaction

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 03.0 (`db.*`, `system.info`, runtime with audit sink), Task 01.0 (audit hook), Task 05.0 (`jobs` table, subtask 21)
**Blocks:** 34
**Estimated size:** S (reduced from M after 06.0 reconciliation)
**Suggested agent:** testing-expert (review: security-auditor)

> **Amended 2026-11-20 (as-built reconciliation).** Reuse the existing
> `tests/unit/commands/commands_test_fakes.hpp` doubles and the registry-build
> pattern from the shipped 03.0/05.0 tests; do **not** add a duplicate
> `commands_test_support.hpp` or a `commands_test::RegistryFixture` (the recon
> found it would duplicate `commands_test_fakes.hpp`). Allowlist, click-events
> redaction, system.info allowlisting, single-record audit and jobs
> metadata-only are already proven; only the gaps below are built. Respect the
> `redactSecretValue` "key" caveat (it does **not** match a key containing
> `key`; the positive allowlist is the real control).

## Objective

Unit-test plan.md contract C6 directly against the handlers: `db.*` exposes
only `links`, `click_events`, `jobs`; no response carries secrets, hashes,
tokens, raw client identifiers or DSNs; `system.info` emits only allowlisted
backend names (and would redact anything else with `::observability::redactSecretValue`);
audit lines carry no argument values.

## Dependencies

- `include/url_shortener/observability/storage_observability.hpp` (`::observability::redactSecretValue(key, value)`, global namespace; verified: redacts keys containing `password`, `secret`, `token` and the password part of `dsn` values - **not** `key`, contrary to CLAUDE.md, correction scheduled in Task 08.0 subtask 06).
- 03.0 handlers and table definitions (`links`, `click_events` in 03.0/16; `jobs` in 05.0/21); 01.0/09 `IAuditSink` (tests install a recording sink through `CommandRuntimeOptions::audit_sink`, 03.0/18).
- Fixtures: reuse `tests/unit/commands/commands_test_fakes.hpp` (`FakeLinkStore`, `FakeStatsReader`, `make_test_config`) and the registry-build pattern already used by the shipped `commands__27_db_commands`, `commands__28_system_info_command`, `commands__09_audit`, `commands__30_browse_security_regression` and `commands__65_jobs_table_source` tests. Do not add a new support header.

## Coverage already delivered (06.0)

| Original 07.0 row | Delivered by (consumed) |
|---|---|
| `tables_exactly_allowlist`, allowlist = links/click_events/jobs | `commands__27_db_commands`, `commands__30_browse_security_regression` |
| `link_rows_do_not_expose_hash_columns`, `click_events_rows_only_client_id_hash` | `commands__30_browse_security_regression` (click_events_table_source path) |
| `db_rows_sort_column_allowlist`, `db_rows_q_filter_is_not_sql`, `db_rows_limit_bounded` | `commands__27_db_commands` |
| `system_info_redacts_secrets` | `commands__28_system_info_command` |
| audit: `*_has_principal_transport_command_outcome`, `*_never_contains_arg_values`, `read_commands_emit_no_audit_line`, `denied_mutation_is_audited_as_denied`, `production_runtime_installs_audit_sink` | `commands__09_audit` |

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/commands/70_db_table_allowlist_test.cpp` | create | hostile-name list + no-existence-oracle (gap) |
| `tests/unit/commands/71_jobs_rows_no_secrets_test.cpp` | create | jobs-row args/result marker scan (gap) |
| `tests/unit/commands/72_system_info_key_set_test.cpp` | create | exact system.info key sets + exception-echo (gap) |
| `sources.cmake` | modify | append to `COMMANDS_UNIT_SOURCES` (list and its `register_labeled_cpp_tests` call are owned by Task 01.0 subtask 01; Task 07.0 uses `tests/unit/commands/70-89`, plan.md "Test file prefixes") |

## Classes and functions

No new support header. Each test builds a registry over `commands_test_fakes`
doubles exactly as `commands__27`/`28` do, and installs a recording
`IAuditSink` through `CommandRuntimeOptions::audit_sink` as `commands__09`
does. A `fake_secret_marker` string and a `configWithSecrets()` config (TLS
cert/key paths, passphrase, `analytics_client_hash_salt`, and a `StorageConfig`
DSN/credentials value, all built at runtime by concatenating the marker so no
credential-looking literal is committed) are defined **locally in file 72**, not
in a shared header.

Gap cases only:

| File | Case | Assertion |
|---|---|---|
| 70 | `rows_rejects_every_non_allowlisted_name` | for `users`, `sessions`, `auth_audit_log`, `schema_migrations`, `sqlite_master`, `pg_catalog.pg_user`, `links;DROP`, `LINKS`, `links ` (trailing space), `../links`, empty: one identical failure code, never `storage_failure` |
| 70 | `not_found_identical_for_unknown_and_forbidden` | responses for `users` and `nope` equal after normalization (no existence oracle) |
| 71 | `jobs_rows_never_expose_args_or_results` | a job whose args and result contain markers: no `jobs` row contains either (05.0/21) |
| 72 | `system_info_emits_only_allowlisted_names` | extra backend option keys named like `x_password`, `api_key`, `my_dsn`, `shared_secret` with marker values never appear in the response at all (03.0/17 emits allowlisted backend names only; `::observability::redactSecretValue` is not even reached - the positive allowlist is the control, see the "key" caveat below) |
| 72 | `system_info_has_no_extra_keys` | top-level key set equals catalog (`version`, `backend`, `persistent`, `read_only`, `shortener`); `shortener`'s own key set equals exactly the nine fields in 03.0/17's `ShortenerSettings` (`base_domain`, `generated_slug_length`, `default_redirect_type`, `default_expiry_seconds`, `allow_private_targets`, `analytics_enabled`, `max_request_body_bytes`, `max_request_target_length`, `request_id_max_length`); new fields on either level need a catalog change |
| 72 | `error_messages_never_echo_secrets` | a fake store/reader throws with the marker in its message; `error.message` lacks the marker |

**`redactSecretValue` "key" caveat (must be encoded, not assumed):**
`::observability::redactSecretValue` redacts keys containing `password`,
`secret`, `token` and the password part of a `dsn` value - it does **not** match
a key containing `key`. `system_info_emits_only_allowlisted_names` therefore
asserts the `api_key`-style keys are absent **because they are never emitted**
(the positive allowlist in `system_commands.cpp`/`ShortenerSettings`), not
because redaction scrubbed them. Do not weaken the case into "redaction scrubbed it".

## Implementation steps

1. Build each registry inline from `commands_test_fakes` doubles (match `commands__27`/`28`); no shared fixture header.
2. Implement 70-72; use `BOOST_TEST_CONTEXT` for the table-name loops.
3. Add files to `COMMANDS_UNIT_SOURCES`, build, run.
4. Request security-auditor review of the expected `links`/`jobs` column lists (the spec of what may be shown).

## Commands

```bash
cmake --build cmake-build --target commands__70_db_table_allowlist_test commands__71_jobs_rows_no_secrets_test commands__72_system_info_key_set_test
ctest --test-dir cmake-build -L unit -R "^commands__7[0-2]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/commands/70_db_table_allowlist_test.cpp` | `commands__70_db_table_allowlist_test` | `unit` | 2 |
| `tests/unit/commands/71_jobs_rows_no_secrets_test.cpp` | `commands__71_jobs_rows_no_secrets_test` | `unit` | 1 |
| `tests/unit/commands/72_system_info_key_set_test.cpp` | `commands__72_system_info_key_set_test` | `unit` | 3 |

## Constraints

- No credential-looking literals in source (the repo `secret-scan` hook blocks them): build fake secrets at runtime from `fake_secret_marker`.
- Absence is asserted by scanning the whole serialized response, not just known fields.
- C++17, Boost.Test, naming per `.clang-tidy`.

## Success criteria

1. Adding a fourth non-allowlisted table name that `db.rows` accepts, or a jobs row that echoes args/results, fails a gap test.
2. All 6 gap cases pass; no copy of the redaction rules exists in tests, and no duplicate `commands_test_support.hpp` was added (grep).
3. security-auditor review recorded in subtask 34.

## Out of scope / risks

- Real Postgres/Redis leakage (integration) is out of scope; handlers go through ports so the in-memory fixture is representative.
- Risk: audit capture mechanism unknown until 01.0; the audit sink may need to be injectable (raise as a 01.0 requirement if not).
