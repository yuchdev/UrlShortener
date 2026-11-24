# TUI Test Plan

## Scope

This plan covers the interactive terminal UI (`url_shortener tui`) defined by
[ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md). The TUI is a
transport adapter over the shared command registry: screens build
`CommandRequest`s and render `CommandResponse`s through `ICommandClient`, and
never touch a service, repository, or DTO directly. The plan maps every ADR
0003 rule (R1-R7), every design diagram (D1-D13), and the Task 06.0/07.0 TUI
subtasks to the automated target that proves them, or to the manual smoke item
(S1-S21, S1b) that covers what automation cannot.

This plan does **not** re-own or re-execute the manual checklist. Task 06.0
owns and executes
[`tui_manual_smoke.md`](/docs/testing/tui_manual_smoke.md) (real terminals,
resize, legacy console, tmux, persistence across restarts, the C5 no-listener
check, and the D1-D13 diagram sign-off). This plan is the glue between the two
layers so that no behavior is covered by neither.

All automated TUI tests are headless: screens render into FTXUI `Screen`
buffers and the client seam is an in-process mock or the real registry, so no
terminal is required. The snapshot harness self-test is
`tui__01_render_harness_selftest`.

## Automated layers

| Layer | Subtask | Target(s) | What it proves |
|---|---|---|---|
| Render harness self-test | 06.0/01 (consumed) | `tui__01_render_harness_selftest` | Deterministic frame capture into a `Screen` buffer; golden compare/update plumbing. |
| Mock-client helpers | 07.0/22 | `tui__22_mock_support_helpers_test` | Fake `ICommandClient`/registry builders used by the screen and flow tests. |
| In-process client seam | 06.0 (consumed) | `tui__02_in_process_client_test`, `tui__03_async_caller_test` | `ICommandClient` returns the registry envelope unchanged; async caller posts back without blocking. |
| Include/layer boundary | 06.0 (consumed) | `tui__02_include_boundary_test`, `commands__79_no_http_server_in_local_modes_test` | Only `ICommandClient` crosses out of `src/tui/`; no service/HTTP/ASIO headers leak in. |
| Navigation / frame goldens | 06.0/24 (consumed) | `tui__04_frame_render_test`, `tui__05_theme_render_test`, `tui__18_readonly_render_test`, `tui__20_app_navigation_test`, `tui__04_router_test` | D1/D11 frame, theme, read-only, and router edge table render to goldens `d01_*`. |
| Screen goldens | 06.0/25 (consumed) | `tui__12_links_screen_test`, `tui__13_link_detail_screen_test`, `tui__14_tables_screen_test`, `tui__15_import_export_screen_test`, `tui__16_jobs_screen_test`, `tui__17_dashboard_screen_test`, `tui__11_palette_render_test`, `tui__08_form_screen_test`, `tui__08_form_widgets_render_test`, `tui__06_dialog_render_test`, `tui__10_table_view_render_test` | D2-D10 layouts render to goldens `d02_*`..`d10_*`. |
| FormModel properties | 07.0/26 | `tui__30_form_model_property_test` | Property-based ArgSpec -> request round-trip, required/optional/nullable omission, JSON escaping (`FORM_PROP_SEED`). |
| State machine / hotkeys / non-blocking | 07.0/27 | `tui__31_screen_transition_requests_test`, `tui__32_hotkeys_and_focus_test`, `tui__33_nonblocking_ui_test` | D11 transition requests match the router edge set; hotkeys vs. input focus; UI never blocks on an in-flight call (proved via `in_flight()`, not wall-clock timing). |
| Confirm / read-only | 07.0/28 (consumed `tui__06/08/18`) + `tui__34/37` | `tui__06_confirm_test`, `tui__08_mutation_flow_test`, `tui__18_readonly_matrix_test`, `tui__37_required_permission_forward_test`, `tui__34_no_secrets_real_registry_test` | Mutating confirms (typed slug), read-only refusals, permission forwarding, and no-secret frames against the real registry. |
| Settings goldens | 06.0/21 (consumed) | `tui__21_settings_screen_test`, `tui__21_settings_model_test` | D13 read-only Settings from `system.info`; goldens `d13_*`. |
| Generic command flow | 06.0 (consumed) | `tui__11_generic_command_flow_test` | Every catalog command selectable and executable from the palette; result view golden; raw-JSON mode. |
| Request envelope goldens | 06.0 (consumed) | `tui__07_form_model_request_test`, `tui__12_links_query_test` | D12 wire envelopes pinned: `07_request_*.json`, `12_request_*.json`. |
| Conformance TUI shim | 07.0/10 | `conformance__02_tui_client_corpus` | D12 same-envelope-on-every-transport: the TUI client runs the whole command corpus identically and deterministically. |
| Secret-field guard | 06.0 (consumed) | `commands__72_system_info_key_set_test` | `system.info` emits only allow-listed names and never echoes secrets (authoritative guard for new `system.info` keys). |

Golden files live under `tests/unit/tui/golden/` (`d01`..`d13`, request
`*.json`, and table/overlay fixtures).

## Manual layer

The manual smoke checklist [`tui_manual_smoke.md`](/docs/testing/tui_manual_smoke.md)
covers items **S1-S21** plus **S1b** and the per-diagram D1-D13 sign-off. It is
owned and executed by Task 06.0 on real terminals. The manual layer carries the
behaviors automation cannot reach: hardware/OS terminal restore, live resize,
legacy-console glyph rendering, cross-process persistence, and human diagram
sign-off.

## Traceability

### ADR 0003 rules (R1-R7)

| Item | Automated (target::case) | Manual | Status |
|---|---|---|---|
| R1 only `ICommandClient` crosses out | `tui__02_include_boundary_test::real_tui_tree_is_clean`, `::direct_client_call_under_screens_is_rejected`; `commands__79_no_http_server_in_local_modes_test::tui_sources_include_no_service_headers`, `::guard_flags_tui_service_header` | none | auto |
| R2 generic first | `tui__11_generic_command_flow_test::every_catalog_command_selectable_and_executable`; `tui__30_form_model_property_test::valid_forms_round_trip_through_validate_args` | S12 (palette) | both |
| R3 never block UI | `tui__33_nonblocking_ui_test::input_accepted_while_request_in_flight`, `::slow_command_does_not_block_render`, `::response_posted_exactly_once` | S5 (dashboard, no freeze), S15 (slow backend) | both |
| R4 mutations confirm | `tui__06_confirm_test::policy_matrix`, `::type_token_exact_match_only`; `tui__08_mutation_flow_test::mutating_confirms_before_call`, `::delete_needs_typed_token` | S7 (create/delete) | both |
| R5 read-only | `tui__18_readonly_matrix_test::every_screen_sends_no_write_in_read_only`, `::registry_denies_write_even_if_client_guard_bypassed`; `tui__37_required_permission_forward_test::in_process_client_required_permission_forwards`; `conformance__02_tui_client_corpus::read_only_user_denied_write_but_admin_allowed` | S13 (read-only) | both |
| R6 headless testable | `tui__01_render_harness_selftest` + every `tui__*` render/flow target | none | auto |
| R7 no secrets on screen | `tui__34_no_secrets_real_registry_test::real_registry_frames_contain_no_secret_markers`, `::tui_filter_also_drops_secret_named_fields`; `tui__14_tables_screen_test`; `commands__72_system_info_key_set_test::system_info_emits_only_allowlisted_names` | S9 (tables), S21 (settings) | both |

### ADR 0003 diagrams (D1-D13)

| Item | Automated (target + golden) | Manual | Status |
|---|---|---|---|
| D1 Frame & nav | `tui__04_frame_render_test`, `tui__05_theme_render_test`, `tui__18_readonly_render_test` -> `d01_*` | S1, S2, S3, S13, S14 | both |
| D2 Dashboard | `tui__17_dashboard_screen_test` -> `d02_*` | S5, S13 | both |
| D3 Links table | `tui__12_links_screen_test`, `tui__10_table_view_render_test` -> `d03_*` | S4, S6, S13 | both |
| D4 Link detail / stats | `tui__13_link_detail_screen_test` -> `d04_*` | S8 | both |
| D5 New / edit form | `tui__08_form_screen_test`, `tui__08_form_widgets_render_test`, `tui__18_readonly_render_test` -> `d05_*`; `tui__30_form_model_property_test` | S7 | both |
| D6 Tables browser | `tui__14_tables_screen_test` -> `d06_*` | S9 | both |
| D7 Import / export | `tui__15_import_export_screen_test`, `tui__18_readonly_render_test` -> `d07_*` | S10 | both |
| D8 Jobs | `tui__16_jobs_screen_test` -> `d08_*` | S10, S11 | both |
| D9 Command palette | `tui__11_palette_render_test`, `tui__11_generic_command_flow_test::result_view_matches_golden`, `tui__18_readonly_render_test` -> `d09_*` | S12 | both |
| D10 Confirm / toast | `tui__06_dialog_render_test` -> `d10_*` | S7 | both |
| D11 Screen state machine | `tui__04_router_test::is_legal_matches_d11_edge_table_exhaustively`, `tui__20_app_navigation_test`, `tui__31_screen_transition_requests_test::table_push_edges_match_router_edge_set_exactly` (no single golden) | S3 | both |
| D12 Same envelope on every transport | `tui__07_form_model_request_test::link_create_request_matches_golden`, `tui__12_links_query_test` -> `07_request_*`/`12_request_*`; `conformance__02_tui_client_corpus::tui_runs_whole_corpus`, `::repeated_run_is_identical` | none (snapshot-only, justified) | auto |
| D13 Settings layout | `tui__21_settings_screen_test` (owned by 06.0/21) -> `d13_*` | S21 | both |

### Diagram-level behaviors beyond the frame goldens

| Item | Automated | Manual | Status |
|---|---|---|---|
| Terminal restore on exception / SIGTERM / normal quit | `tui__04_terminal_restore_test::exception_from_loop_restores_once_and_propagates`, `::simulated_sigterm_restores_line_discipline_before_escapes`, `::restore_is_idempotent` | S1 (clean quit), S1b (Ctrl-C while typing) | both |
| Terminal restore after hard crash / `SIGKILL` | none (SIGKILL is uncatchable by design) | gap - see Known gaps | gap |
| Live resize | none | S2 | manual |
| Legacy console glyphs | none | S1, S14 | manual |
| IME / wide characters | partial: `tui__30_form_model_property_test::every_byte_survives_json_escaping` (JSON escaping only) | S20 (CJK via target URL / metadata; tags reject non-ASCII by design) | both |

### Task subtask -> target map

| Subtask | Proves | Target(s) |
|---|---|---|
| 06.0/01 snapshot harness | R6 | `tui__01_render_harness_selftest` |
| 06.0/21 Settings goldens (D13) | D13, R7 | `tui__21_settings_screen_test`, `tui__21_settings_model_test` |
| 06.0/24 navigation goldens | D1, D11 | `tui__04_frame_render_test`, `tui__04_router_test`, `tui__20_app_navigation_test` |
| 06.0/25 forms/jobs/palette/screen goldens | D2-D10 | screen golden targets above |
| 07.0/22 mock-client helpers | tooling | `tui__22_mock_support_helpers_test` |
| 07.0/26 FormModel properties | R2, D5 | `tui__30_form_model_property_test` |
| 07.0/27 state machine / hotkeys / non-blocking | R3, D11 | `tui__31_screen_transition_requests_test`, `tui__32_hotkeys_and_focus_test`, `tui__33_nonblocking_ui_test` |
| 07.0/28 confirm / read-only | R4, R5 | `tui__06_confirm_test`, `tui__08_mutation_flow_test`, `tui__18_readonly_matrix_test`, `tui__34_no_secrets_real_registry_test`, `tui__37_required_permission_forward_test` |
| 07.0/10 conformance shim | D12, R5 | `conformance__02_tui_client_corpus` |
| 06.0 service-header guard | R1 | `commands__79_no_http_server_in_local_modes_test` |

## Acceptance criteria

1. Every ADR 0003 rule R1-R7 has at least one automated target, and every row
   is `auto`, `both`, or an owned `gap`.
2. Every diagram D1-D13 maps to at least one passing automated target (frame,
   request, or conformance) or an explicit snapshot-only justification (D12),
   plus its manual sign-off item.
3. Every cited target appears in `ctest --test-dir cmake-build -N -R "^tui__"`
   (or the `conformance__`/`commands__` equivalents); every cited golden exists
   under `tests/unit/tui/golden/`.
4. No `gap` row exists without an owner recorded in Known gaps.

## Commands

```bash
# List / run every TUI unit target
ctest --test-dir cmake-build -N -R "^tui__"
ctest --test-dir cmake-build -R "^tui__" --output-on-failure

# Conformance TUI shim and service-header / secret guards
ctest --test-dir cmake-build -R "conformance__02_tui_client_corpus|commands__79|commands__72" --output-on-failure

# Regenerate screen goldens after an intentional layout change
UPDATE_GOLDEN=1 ctest --test-dir cmake-build -R "^tui__" --output-on-failure

# Reproduce a FormModel property failure from its seed
FORM_PROP_SEED=<seed> ctest --test-dir cmake-build -R "^tui__30_" --output-on-failure
```

## Known gaps

| Gap | Owner | Disposition |
|---|---|---|
| Terminal restore after a hard crash / `SIGKILL` | Task 06.0 manual pass | `SIGKILL` cannot be caught, so termios/line-discipline recovery after a hard kill is an OS-level behavior that `tui__04_terminal_restore_test` cannot exercise (it covers exception, `SIGTERM`, and normal-quit restore). Recorded as a ratified deferred human check; no dedicated manual step is added here without 06.0 owner agreement. |
| Windows Terminal, legacy conhost, Linux/WSL terminals, `tmux`, PowerShell `Get-NetTCPConnection` | Task 06.0 manual pass | Ratified deferred human pass: only macOS was available for the recorded run; these rows are marked **NOT RUN - requires `<platform>`** in `tui_manual_smoke.md`, never as passes. |
| Live resize and legacy-console glyph rendering | Task 06.0 manual pass | Manual-only (S2, S14); not reachable by the headless render harness beyond the pty harness's `TIOCSWINSZ` on macOS. |
| IME / wide-character input beyond JSON escaping | Task 06.0 manual pass | Automated coverage is JSON-escaping only (`tui__30_form_model_property_test::every_byte_survives_json_escaping`); on-screen wide-character display is manual (S20). |
| Cross-process analytics for D4 stats (S8) | Task 06.0 (Milestone 0006 limitation) | No cross-process analytics store exists in 0006 (`noop`/`inmemory` only), so a TUI process cannot observe a separate server's redirect counts; S8's stats path is not fully runnable. |

### Notes on guard ownership

- `tui__34_no_secrets_real_registry_test` checks that rendered frames and the
  TUI field filter carry no secret markers, but it does **not** catch a *new*
  secret key added to the `system.info` JSON. The authoritative guard for new
  `system.info` keys is `commands__72_system_info_key_set_test`
  (`system_info_emits_only_allowlisted_names`).
- The non-blocking proof (`tui__33_nonblocking_ui_test`) asserts on the
  client's `in_flight()` state rather than wall-clock timing, by policy, to stay
  deterministic.
