# 18 - Security: allowlist and redaction

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 03.0 (`db.*`, `system.info`, runtime with audit sink), Task 01.0 (audit hook), Task 05.0 (`jobs` table, subtask 21)
**Blocks:** 34
**Estimated size:** M
**Suggested agent:** testing-expert (review: security-auditor)

## Objective

Unit-test plan.md contract C6 directly against the handlers: `db.*` exposes
only `links`, `click_events`, `jobs`; no response carries secrets, hashes,
tokens, raw client identifiers or DSNs; `system.info` emits only allowlisted
backend names (and would redact anything else with `::observability::redactSecretValue`);
audit lines carry no argument values.

## Dependencies

- `include/url_shortener/observability/storage_observability.hpp` (`::observability::redactSecretValue(key, value)`, global namespace; verified: redacts keys containing `password`, `secret`, `token` and the password part of `dsn` values - **not** `key`, contrary to CLAUDE.md, correction scheduled in Task 08.0 subtask 06).
- 03.0 handlers and table definitions (`links`, `click_events` in 03.0/16; `jobs` in 05.0/21); 01.0/09 `IAuditSink` (tests install a recording sink through `CommandRuntimeOptions::audit_sink`, 03.0/18).
- Pattern: real in-memory ports as in `tests/unit/security/` and `tests/unit/app/app_test_fakes.hpp`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/commands/70_db_table_allowlist_test.cpp` | create | allowlist cases |
| `tests/unit/commands/71_db_column_secret_filter_test.cpp` | create | column/field leak cases |
| `tests/unit/commands/72_system_info_redaction_test.cpp` | create | redaction cases |
| `tests/unit/commands/73_audit_line_no_args_test.cpp` | create | audit cases |
| `tests/unit/commands/commands_test_support.hpp` | create | fixture: registry over in-memory ports, helper `exec(role, command, args_json)` |
| `sources.cmake` | modify | append to `COMMANDS_UNIT_SOURCES` (list and its `register_labeled_cpp_tests` call are owned by Task 01.0 subtask 01; Task 07.0 uses `tests/unit/commands/70-89`, plan.md "Test file prefixes") |

## Classes and functions

```cpp
namespace commands_test {
struct RegistryFixture {                      // in-memory ports only; distinct from conformance::RegistryFixture
    explicit RegistryFixture(StorageConfig cfg = defaultTestConfig());
    std::string exec(std::string_view role, std::string_view command, std::string_view args_json);
    std::vector<std::string> audit_lines;     // captured via the audit sink / logger redirect
};
// Builds a config whose DSN, cache password and analytics salt contain the
// marker FAKE_SECRET_MARKER, assembled at runtime (string concatenation) so
// no credential-looking literal exists in the source file.
StorageConfig configWithSecrets();
extern const std::string fake_secret_marker;
}
```

Cases:

| File | Case | Assertion |
|---|---|---|
| 70 | `tables_exactly_allowlist` | `db.tables` names == {`links`,`click_events`,`jobs`} |
| 70 | `rows_rejects_every_non_allowlisted_name` | for `users`, `sessions`, `auth_audit_log`, `schema_migrations`, `sqlite_master`, `pg_catalog.pg_user`, `links;DROP`, `LINKS`, `links ` (trailing space), `../links`, empty: one identical failure code, never `storage_failure` |
| 70 | `not_found_identical_for_unknown_and_forbidden` | responses for `users` and `nope` equal after normalization (no existence oracle) |
| 71 | `link_rows_do_not_expose_hash_columns` | every key of every `links` row is in an explicit expected-column list |
| 71 | `click_events_rows_only_client_id_hash` | seed an event through the real sanitizer path; row has `client_id_hash` and no raw IP or client identifier field |
| 71 | `jobs_rows_never_expose_args_or_results` | a job whose args and result contain markers: no `jobs` row contains either (05.0/21) |
| 71 | `db_rows_sort_column_allowlist` | sort on an unknown column -> `invalid_field` |
| 71 | `db_rows_q_filter_is_not_sql` | `q` = a quote-or-1-equals-1 injection string returns zero rows and no backend error text |
| 71 | `db_rows_limit_bounded` | `limit: 501` -> `invalid_field` |
| 72 | `system_info_redacts_secrets` | with `configWithSecrets()` (a `ServerConfig` with TLS cert/key paths, passphrase and `analytics_client_hash_salt` set to recognizable sentinels, plus a `StorageConfig` with a DSN/credentials sentinel), the serialized response does not contain `fake_secret_marker`, any TLS file path, the passphrase sentinel or the salt sentinel |
| 72 | `system_info_emits_only_allowlisted_names` | extra backend option keys named like `x_password`, `api_key`, `my_dsn`, `shared_secret` with marker values never appear in the response at all (03.0/17 emits allowlisted backend names only; `::observability::redactSecretValue` is not even reached) |
| 72 | `system_info_has_no_extra_keys` | top-level key set equals catalog (`version`, `backend`, `persistent`, `read_only`, `shortener`); `shortener`'s own key set equals exactly the nine fields in 03.0/17's `ShortenerSettings` (`base_domain`, `generated_slug_length`, `default_redirect_type`, `default_expiry_seconds`, `allow_private_targets`, `analytics_enabled`, `max_request_body_bytes`, `max_request_target_length`, `request_id_max_length`); new fields on either level need a catalog change |
| 72 | `error_messages_never_echo_secrets` | a fake repository throws with the marker in its message; `error.message` lacks the marker |
| 73 | `audit_line_has_principal_transport_command_outcome` | one line per mutating command |
| 73 | `audit_line_never_contains_arg_values` | create with a distinctive target URL path and a metadata value; no audit line contains either |
| 73 | `read_commands_emit_no_audit_line` | `link.get`, `db.rows`, `command.list` |
| 73 | `denied_mutation_is_audited_as_denied` | `user` calling `link.delete` yields an audit line with outcome `not_authorized` and no state change |
| 73 | `production_runtime_installs_audit_sink` | `build_command_runtime` with default options has a `LoggerAuditSink`; `link.create` produces exactly one record |

## Implementation steps

1. Write the shared fixture header first (about 80 lines); keep it free of transport code.
2. Implement 10-13; use `BOOST_TEST_CONTEXT` for table-name loops.
3. Add files to `COMMANDS_UNIT_SOURCES`, build, run.
4. Request security-auditor review of the expected `links` column list (it is the spec of what may be shown).

## Commands

```bash
cmake --build cmake-build --target commands__70_db_table_allowlist_test commands__71_db_column_secret_filter_test commands__72_system_info_redaction_test commands__73_audit_line_no_args_test
ctest --test-dir cmake-build -L unit -R "^commands__7[0-3]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/commands/70_db_table_allowlist_test.cpp` | `commands__70_db_table_allowlist_test` | `unit` | 3 |
| `tests/unit/commands/71_db_column_secret_filter_test.cpp` | `commands__71_db_column_secret_filter_test` | `unit` | 6 |
| `tests/unit/commands/72_system_info_redaction_test.cpp` | `commands__72_system_info_redaction_test` | `unit` | 4 |
| `tests/unit/commands/73_audit_line_no_args_test.cpp` | `commands__73_audit_line_no_args_test` | `unit` | 5 |

## Constraints

- No credential-looking literals in source (the repo `secret-scan` hook blocks them): build fake secrets at runtime from `fake_secret_marker`.
- Absence is asserted by scanning the whole serialized response, not just known fields.
- C++17, Boost.Test, naming per `.clang-tidy`.

## Success criteria

1. Adding a fourth table to `db.tables` or a hash column to `links` rows fails a test.
2. All 18 cases pass; no copy of the redaction rules exists in tests (the real `::observability::redactSecretValue` is the only rule).
3. security-auditor review recorded in subtask 34.

## Out of scope / risks

- Real Postgres/Redis leakage (integration) is out of scope; handlers go through ports so the in-memory fixture is representative.
- Risk: audit capture mechanism unknown until 01.0; the audit sink may need to be injectable (raise as a 01.0 requirement if not).
