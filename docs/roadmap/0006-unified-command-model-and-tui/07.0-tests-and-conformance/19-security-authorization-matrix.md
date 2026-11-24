# 19 - Security: authorization matrix

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Tasks 01.0 (`AccessGuard` in `execute`), 05.0 (`--read-only`, stdio), 04.0 (REST caller), 06.0 (`ICommandClient::required_permission`, `InProcessClient`)
**Blocks:** 34
**Estimated size:** M
**Suggested agent:** testing-expert (review: security-auditor)

> **Amended 2026-11-20 (as-built reconciliation).** This spec (dated 2026-10-22)
> predated the 2026-11-19 client-preflight ruling. It now adds the registry-level
> `resolve_permission` / `effective_permission` **raised-permission** assertions
> for `link.list status=all|deleted` and `link.export path`, and the
> `ICommandClient::required_permission` forward-equivalence - exactly the gap that
> produced the 06.0 HIGH x2 finding (`c770499`/`ec1cfe7`). Reuse
> `tests/unit/commands/commands_test_fakes.hpp`; there is no
> `commands_test::RegistryFixture`.

## Objective

Prove plan.md C7: authorization lives in the registry, not in adapters.
Unit-test the permission check against every descriptor, and process-level test
that the local transports honor `--read-only` and that REST honors its
principal, with `not_authorized` returned before any side effect.

## Dependencies

- `include/url_shortener/security/` (`ControlSet`, `AccessGuard::requireRead/requireWrite/requireMigration/requireUserManagement`; existing tests `tests/unit/security/01_control_set_test.cpp`, `02_access_guard_test.cpp`).
- Subtask 14 (corpus authz matrix) covers envelope-level behavior; this subtask covers what the corpus cannot: exhaustive descriptor iteration, `migrate`/`manage_users` permissions, argument-dependent raise, and adapters bypass attempts.
- Reuse `tests/unit/commands/commands_test_fakes.hpp` and the registry-build pattern of the shipped `commands__06/07/08/59/30` tests.

## Coverage already delivered (06.0)

| Original 07.0 row | Delivered by (consumed) |
|---|---|
| ordering: authz before argspec / before side effect | `commands__06`, `commands__08`, `commands__30` |
| `read_only_export_to_path_denied`, export path raise behavior | `commands__59` (pair commands) |
| `--read-only` on `cmd`/`serve-stdio`, closed-envelope role rejection | `cli/21`, `cli/40` integration |
| REST gate matrix (default off / user denied write / admin loopback-only / resource routes unchanged) | `http/24` |

## Choke points (threat brief)

| Control | Location |
|---|---|
| `max(base, permission_for(args))` | `src/commands/command_registry.cpp` `effective_permission` (~:98) |
| Public read path used by the preflight | `command_registry.cpp` `resolve_permission` (~:769) |
| `link.list` raise (`status=all\|deleted` -> write) | `src/commands/link_list_command.cpp` `link_status_permission` (~:299) |
| `link.export` raise (`path` -> write) | `src/commands/pair_commands.cpp` (~:1063) |
| Client mirror forwards to the registry | `src/tui/in_process_client.cpp` `required_permission` (~:184); declared on `ICommandClient` (`include/url_shortener/tui/command_client.hpp`), relayed by `AsyncCaller::required_permission`, consumed by `CapabilityModel::can_run(info, args)` |

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/commands/74_authz_descriptor_matrix_test.cpp` | create | in-process, all descriptors x all `ControlSet`s; **plus** the `resolve_permission` raised-permission cases below |
| `tests/unit/commands/75_authz_before_side_effects_test.cpp` | create | no-mutation proofs |
| `tests/unit/tui/37_required_permission_forward_test.cpp` | create | `InProcessClient::required_permission` forward-equivalence (needs the real registry + TUI client; target `tui__37_*`, cross-listed under 06.0's TUI list - cpp-expert/testing-expert pick model vs render list) |
| `tests/conformance/py/30_authz_process_level.py` | create | `--read-only` for `cmd`, `serve-stdio`; REST principal tests |
| `sources.cmake` | modify | Append to `COMMANDS_UNIT_SOURCES`, the owning TUI list (06.0/01), and `CONFORMANCE_PY_SOURCES` |

## Classes and functions

Unit (build the registry inline from `commands_test_fakes` doubles):

Raised-permission cases added to file 74 (the 2026-11-19 ruling (b)):

| File | Case | Assertion |
|---|---|---|
| 74 | `resolve_permission_link_list_status_raises` | `resolve_permission(link.list, status=active) == read`; `status=all == write`; `status=deleted == write` (raise at the descriptor, not only at the execute boundary; guards T-71) |
| 74 | `resolve_permission_link_export_path_raises` | `resolve_permission(link.export, {path:...}) == write`; inline export (no `path`) == `read` (guards T-9/T-118) |
| 74 | `preflight_matches_executed_effective_permission` | for both raising commands, `effective_permission(args)` used by `execute` equals `resolve_permission(args)` used by the preflight (single source of truth) |
| 37 (tui) | `in_process_client_required_permission_forwards` | `InProcessClient::required_permission(req) == permission_name(resolve_permission(req))` for `link.list status=all` and `link.export path`; the client never re-implements the rule (extends contract C8's client interface) |

| File | Case | Assertion |
|---|---|---|
| 74 | `every_descriptor_has_explicit_permission` | iterate `registry.list()`: `permission` is one of read/write/migrate/manage_users (no default-initialized value) |
| 74 | `permission_matches_catalog` | per command, descriptor perm equals the catalog legend (reuse parser from subtask 16 or hard list generated from `command.list`) |
| 74 | `mutating_implies_write_or_higher` | `mutating == true` implies perm != read |
| 74 | `user_controlset_denied_all_non_read` | for each descriptor with perm != read, `CallerContext{User}` gets `not_authorized`, using minimal valid args |
| 74 | `admin_controlset_allowed_write_and_migrate` | Admin passes the guard (result may be any non-`not_authorized` outcome) |
| 74 | `manage_users_reserved` | no descriptor with `manage_users` exists until 0002 Task 07.0 (guard against accidental registration); test documents the expectation and is updated by that task |
| 74 | `rest_policy_refuses_manage_users` | a stub `manage_users` descriptor executed through the REST bridge (`rest_policy_refusal`, 04.0/02) is refused even for `Admin` |
| 75 | `denied_call_does_not_touch_repository` | counting fake `IMetadataRepository` records zero calls on a denied `link.create`/`link.update`/`link.delete` |
| 75 | `denied_call_does_not_enqueue_job` | `link.import` as `user` leaves `job.list` empty and the runner idle |
| 75 | `authorization_precedes_argspec_validation` | invalid args as `user` -> `not_authorized` |
| 75 | `unknown_command_precedes_authorization` | `user` + unknown command -> `unknown_command` |
| 75 | `dry_run_still_requires_permission` | `user` + `options.dry_run` on `link.create` -> `not_authorized` |
| 75 | `read_only_export_to_path_denied` | `user` + `link.export` with `path` -> `not_authorized`, no file created; inline export allowed |

Process level (`30_authz_process_level.py`, `unittest`):

| Test | Assertion |
|---|---|
| `test_cmd_read_only_denies_mutation` | `cmd --read-only link.create ...` exits 5 with `not_authorized`; `link.get` exits 0 |
| `test_cmd_without_read_only_allows` | baseline admin |
| `test_stdio_read_only` | `serve-stdio --read-only`: create denied, get allowed, process stays up |
| `test_read_only_flag_cannot_be_overridden_by_args` | `args` or `options` keys such as `caller`, `role`, `read_only`, `principal` -> `invalid_field` (not honored) |
| `test_system_info_reports_read_only` | `system.info` `read_only` true |
| `test_rest_gate_default_off` | default server flags: `POST /api/v1/commands`, `GET /api/v1/links`, `GET /api/v1/jobs` answer exactly like an unmatched route (404 `Route not found`) |
| `test_rest_user_role_denied_write` | `--command-api-enabled true` (default role `user`): `link.create` over the generic endpoint -> 403 `not_authorized`, `link.list` -> 200; `POST /api/v1/jobs/{id}/cancel` -> 403 |
| `test_rest_admin_role_loopback_only` | `--command-api-role admin`: from 127.0.0.1 `link.create` -> 200; from a non-loopback peer (when the host has one; else skipped with message) -> 403 |
| `test_rest_resource_routes_unchanged` | resource route `POST /api/v1/links` keeps today's open behavior (201) regardless of the command-API flags (C3) |

## Implementation steps

1. Unit tests first (no process). 2. Process tests reuse `conformance_common` + `conformance_rest`. 3. Compare denial status with existing REST behavior from `docs/testing/testplans/security.md`.

## Commands

```bash
ctest --test-dir cmake-build -R "^(commands__7[45]_|tui__37_|conformance_30_)" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/commands/74_authz_descriptor_matrix_test.cpp` | `commands__74_authz_descriptor_matrix_test` | `unit` | 7 + 3 raised-permission |
| `tests/unit/commands/75_authz_before_side_effects_test.cpp` | `commands__75_authz_before_side_effects_test` | `unit` | 6 |
| `tests/unit/tui/37_required_permission_forward_test.cpp` | `tui__37_required_permission_forward_test` | `unit` | 1 |
| `tests/conformance/py/30_authz_process_level.py` | `conformance_30_authz_process_level` | `integration;conformance` | 9 |

## Constraints

- The auth broker rule stays: tests never bind `0.0.0.0` and never expose the broker.
- Tests must iterate descriptors dynamically so new commands are covered automatically.
- No hard-coded tokens in committed files; test principals are created in-process or via the existing first-admin bootstrap helpers used in `tests/unit/security/05_first_admin_bootstrap_test.cpp`.

## Success criteria

1. Registering a mutating command with `permission = read` fails `mutating_implies_write_or_higher`.
2. A denied call causes zero repository/job side effects.
3. `--read-only` is verified on both `cmd` and `serve-stdio` processes (TUI covered in subtask 28).

## Out of scope / risks

- Real session/cookie/CSRF auth is Milestone 0002.
- Risk: REST caller derivation unresolved (subtask 08); two tests may skip until 04.0 decides - the gate in subtask 34 lists any remaining skips as blockers.
