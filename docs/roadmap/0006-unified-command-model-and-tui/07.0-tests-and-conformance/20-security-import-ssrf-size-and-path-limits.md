# 20 - Security: import SSRF, size and path limits

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 05.0 (`link.import`, `link.export`)
**Blocks:** 34
**Estimated size:** M
**Suggested agent:** testing-expert (review: security-auditor)

## Objective

Treat import/export as the new untrusted-input surface (CLAUDE.md: all
user-supplied URLs, slugs, bodies, plus file paths): prove bulk rows go through
the same SSRF/URL/slug validation as `link.create` (no second path), oversize
payloads are rejected, `path` cannot escape or hit special files, and REST cannot read or
write local files.

## Dependencies

- `include/url_shortener/core/utils.hpp` (`isPrivateHost`, `SHORTENER_ALLOW_PRIVATE_TARGETS` / `shortener_allow_private_targets`), request hardening limits (`--max-request-body-bytes` default 65536).
- 05.0 decisions this subtask asserts (Task 05.0 subtasks 06, 07, 10, 16; decision log Q6): without `--path-root` any path is allowed for the local operator (no `..` restriction); with `serve-stdio --path-root <dir>` the canonicalized path (symlinks resolved, component-wise prefix) must lie inside the root, else `invalid_field`; `--no-file-paths` -> `unsupported`; UNC/device paths and special files rejected; import file cap `kMaxImportFileBytes` (64 MiB), line cap `kMaxImportLineBytes`, inline `data` cap `max_request_body_bytes`; export is create-new only (never overwrites); `path` on REST -> `unsupported`; export to `path` by a read-only caller -> `not_authorized`; CSV cells are not formula-escaped because targets are `http(s)://` and slugs match `^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$`.
- Existing oracle: `tests/unit/core/*` URL validation tests for the SSRF vocabulary (private ranges, localhost, link-local, IPv6 forms, userinfo tricks).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/commands/76_import_row_validation_parity_test.cpp` | create | parity with `link.create` |
| `tests/unit/commands/77_import_limits_test.cpp` | create | size, rows, line length, error cap |
| `tests/unit/commands/78_path_argument_policy_test.cpp` | create | traversal, special files, symlinks, root policy |
| `tests/conformance/py/31_import_limits_process_level.py` | create | REST body limit and path-on-REST, stdio line limit |
| `sources.cmake` | modify | Append to `COMMANDS_UNIT_SOURCES`, `CONFORMANCE_PY_SOURCES` |

## Classes and functions

Unit cases (fixture from subtask 18; job runner run synchronously via the test-only `JobRunner` drain helper from 05.0 or by polling `job.get`):

| File | Case | Assertion |
|---|---|---|
| 76 | `import_row_invalid_url_matches_create` | for each URL in a shared vector list (private IPv4/IPv6, `localhost`, `0.0.0.0`, decimal/octal/hex IPs, userinfo `@` tricks, `file:`, `javascript:`, `data:`, overlong), the row error code equals the `link.create` code for the same input (build the vector list once in a header and use it in both) |
| 76 | `import_row_slug_rules_match_create` | invalid/reserved/conflicting slug per row == create codes |
| 76 | `import_allow_private_targets_flag_honored` | with `shortener_allow_private_targets=true` private rows succeed, exactly as create |
| 76 | `import_private_row_does_not_resolve_dns` | validation is syntactic: a fake resolver/clock hook records zero lookups (if no hook, assert via timing < 50 ms for 1000 rows) |
| 76 | `import_ndjson_args_validated_by_argspec` | NDJSON row with unknown key -> row error `invalid_field`, others continue |
| 76 | `import_dry_run_validates_without_writing` | repository write count is 0 |
| 77 | `inline_data_over_max_body_rejected` | `data` > `max_request_body_bytes` -> `invalid_field`, no job created |
| 77 | `file_size_cap` | file larger than `kMaxImportFileBytes` -> `invalid_field` before reading (size seam) |
| 77 | `single_line_length_cap` | 1 MiB line -> row error, memory bounded |
| 77 | `errors_array_capped_at_100` | 1000 bad rows -> `errors.size() == 100`, `failed == 1000` |
| 77 | `csv_edge_cases` | quotes, embedded commas/newlines, BOM, CRLF, blank lines, trailing comma, 3+ columns -> defined per-row errors, never a crash |
| 77 | `export_data_size_cap` | inline export larger than `kMaxInlineExportBytes` -> job `failed`/`invalid_field`, no partial `data` |
| 78 | `path_root_policy` | with `PathPolicy{root=<tmp>/r}`: `<tmp>/r/a.csv` accepted; `../x`, `..\\x`, `a/../../x`, `<tmp>/rx/a.csv` (string-prefix trap), an absolute path outside the root -> `invalid_field` and the file is NOT opened (sentinel file outside the root unmodified); without a root the same relative paths resolve normally (local operator) |
| 78 | `path_disabled_policy` | `PathPolicy{allow_paths=false}` -> `unsupported` for import and export, no filesystem access |
| 78 | `path_special_files_rejected` | `\\\\.\\COM1`, `NUL`, `CON`, UNC `\\\\host\\share\\f`, `/dev/zero`, `/proc/self/environ`, directory, named pipe -> rejected before read (Windows names only on Windows; use `#ifdef _WIN32` guarded cases) |
| 78 | `path_symlink_escape_rejected_under_root` | POSIX: a symlink inside the root pointing outside -> `invalid_field` (canonicalization); skipped on Windows without privilege |
| 78 | `path_nul_and_overlong` | embedded NUL, > 4096 chars -> `invalid_field` |
| 78 | `export_path_never_overwrites` | exporting to an existing file -> `invalid_field` ("file exists"), file untouched; no temp residue |
| 78 | `export_cells_cannot_start_with_formula_chars` | property check: for generated slugs and every accepted target, no CSV cell starts with `=`, `+`, `-`, `@` (safe by construction, no escaping) |
| 78 | `read_only_export_to_path_denied` | `ControlSet::User` + `link.export` with `path` -> `not_authorized`, no file |

Process level (`31_import_limits_process_level.py`): `test_rest_import_path_unsupported` (REST generic `link.import` with `path` -> `unsupported`, file not read: sentinel file with unique content is not echoed in any response), `test_rest_export_path_unsupported`, `test_rest_body_over_limit_rejected_before_parse` (413 or catalog code; server alive), `test_stdio_oversize_line_survives`, `test_stdio_path_root_confines` (`serve-stdio --path-root <tmp>`: escaping path -> `invalid_field`), `test_stdio_no_file_paths` (`--no-file-paths`: `path` -> `unsupported`), `test_cmd_path_traversal_exit_code` (exit 2 for a nonexistent file).

## Implementation steps

1. Extract the shared URL vector list to `tests/unit/commands/ssrf_vectors.hpp` (also reusable by the 03.0/07 `link.create` tests).
2. Write unit cases; for filesystem cases use a per-test temp directory (`std::filesystem::temp_directory_path()/uuid`), cleaned in a fixture destructor.
3. Process tests reuse `conformance_common`.

## Commands

```bash
ctest --test-dir cmake-build -R "^(commands__7[6-8]_|conformance_31_)" --output-on-failure
# Windows: add -C Debug; POSIX-only cases self-skip with a message
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/commands/76_import_row_validation_parity_test.cpp` | `commands__76_import_row_validation_parity_test` | `unit` | 6 |
| `tests/unit/commands/77_import_limits_test.cpp` | `commands__77_import_limits_test` | `unit` | 6 |
| `tests/unit/commands/78_path_argument_policy_test.cpp` | `commands__78_path_argument_policy_test` | `unit` | 8 |
| `tests/conformance/py/31_import_limits_process_level.py` | `conformance_31_import_limits_process_level` | `integration;conformance` | 7 |

## Constraints

- No network access: never resolve DNS; never fetch URLs from imported rows.
- Filesystem tests write only under the temp dir; sentinel files are created there.
- Memory bounded: the 1 MiB line case asserts peak behavior indirectly (completes within 2 s).

## Success criteria

1. Import and create produce identical codes for the whole SSRF vector list (single shared list).
2. No test can read outside its temp dir; sentinel content never appears in any response.
3. REST rejects `path` on both pair commands.

## Out of scope / risks

- Fuzzing the CSV parser at scale: a time-boxed libFuzzer harness is a possible follow-up, not required.
- The path policy is decided (05.0/07 rule 6, decision Q6); if open decision review changes it, update `path_root_policy` accordingly.
