# 28 - TUI confirm and read-only tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23; Task 06.0 (read-only and safety behaviors), Task 03.0/05.0 (real registry for the no-secrets test)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** XS (most of this shipped in 06.0)
**Suggested agent:** testing-expert (review: security-auditor)

> **Amended 2026-11-20 (as-built reconciliation).** Confirm flows, read-only
> matrix/render, capability gating and row-JSON secret filtering shipped in 06.0.
> The only gap is the **real-registry no-secrets** test (originally test 62). Per
> the 2026-11-20 ruling it **drops the "TUI has no redaction logic" structural
> assertion** and instead keeps TUI redaction as **defense in depth**: it asserts
> (a) the server handlers are the primary control - real-registry frames of
> Tables / System info / Dashboard contain no secret markers (the positive
> allowlist in handlers, subtask 18) - **and** (b) the TUI filter also drops
> secret-named fields (T-125). New stem: `tui__34`. `configWithSecrets()` does not
> exist as a shared helper; build it locally from `fake_secret_marker` (subtask 18
> no longer ships a `RegistryFixture`).

## Coverage already delivered (06.0)

| Need | Delivered by (consumed) |
|---|---|
| confirm dialog gating, typed-slug delete, y/n confirm, `--yes` skip | `tui__06_confirm_test`, `tui__06_dialog_render_test`, `tui__08_mutation_flow_test` |
| generic-form confirm before submit | `tui__11_generic_command_flow_test` |
| read-only matrix / render / capability gate (arg-aware) | `tui__18_readonly_matrix_test`, `tui__18_readonly_render_test`, `tui__18_capability_test` |
| read-only goldens | `d01_frame_readonly_80x24`, `d03_links_readonly_80x24`, `d05_new_link_readonly_80x24`, `d07_import_readonly_80x24`, `d09_palette_readonly_80x24`, `d13_settings_readonly_80x24` |
| row-JSON drops secret-named fields (TUI defense in depth) | `tui__14_tables_screen_test::row_json_hides_secret_fields` |

## Dependencies

- Subtasks 22 (`tui_test_support.hpp`, `command_list_fixture`), 23; 06.0 read-only/safety behaviors; Task 10 (`InProcessClient` shim) for the real-registry frame.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/34_no_secrets_real_registry_test.cpp` | create | the real-registry no-secrets test (stem `34` is free) |
| `sources.cmake` | modify | Append to the owning TUI list (render: `TUI_RENDER_UNIT_SOURCES`) |

## Classes and functions

The single gap case (ruling 2026-11-20):

| File | Case | Assertion |
|---|---|---|
| 34 | `real_registry_frames_contain_no_secret_markers` | build `InProcessClient` over the real `build_command_runtime` registry with a locally-built `configWithSecrets()` (TLS paths, passphrase, `analytics_client_hash_salt`, a `StorageConfig` DSN - all concatenated at runtime from `fake_secret_marker`); render Tables, System info and Dashboard; **no** frame contains any marker. This proves the **server handlers are the primary control** (positive allowlist, subtask 18). |
| 34 | `tui_filter_also_drops_secret_named_fields` | script a mock `db.rows` response that (as only a handler bug could) carries fields named `password`/`token`/`salt`/`dsn`: the TUI's own row filter drops them (defense in depth, T-125). **Not** a structural "no redaction logic" assertion - the TUI keeps its filter. |

## Implementation steps

1. Build the `InProcessClient` over the real registry with a locally-assembled `configWithSecrets()` (subtask 18 ships no shared helper); drive the Tables/System/Dashboard screen components directly (no `ScreenFixture`).
2. For the defense-in-depth case, use `MockCommandClient` with a scripted secret-bearing `db.rows` response and assert the rendered frame drops the secret-named fields.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__34_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/34_no_secrets_real_registry_test.cpp` | `tui__34_no_secrets_real_registry_test` | `unit` | 2 |

## Constraints

- Fake secret strings are assembled at runtime (repo secret-scan hook).
- Read-only is a registry-enforced property (proven by the shipped `tui__18_*`); this subtask only adds the no-secrets proof.
- Keep the TUI secret-named-field filter (defense in depth); do **not** assert the TUI has "no redaction logic".

## Success criteria

1. No real-registry frame contains the secret marker.
2. The TUI filter still drops secret-named fields (T-125).

## Out of scope / risks

- Confirm/read-only behavior is already proven by the shipped `tui__06`/`08`/`11`/`18` tests (consumed).
