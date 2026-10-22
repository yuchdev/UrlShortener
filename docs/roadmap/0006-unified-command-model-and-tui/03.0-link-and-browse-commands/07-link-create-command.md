# 07 - link.create command

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 03.0/05 (codec), 03.0/06 (`ValidateCreateLink`), 01.0/05-07 (`ArgSpec`, descriptor, `execute`)
**Blocks:** 11, 05.0/08
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Define the `link.create` descriptor (arguments per [catalog 3.1](/docs/commands/README.md))
and its handler: decode validated args into `app::CreateLinkCommand`, call
`LinkCommandService::CreateLink` (or `ValidateCreateLink` when `dry_run`), and return the
body produced by `app::serializeLinkViewJson`. The command accepts only the canonical
`target_url`; the REST-only `url` alias is a projection concern (Task 04.0, decision-log Q18).

## Dependencies

- Codec helpers from subtask 05 and the dry-run service methods from subtask 06.
- 01.0 (specified in 01.0/05-07): `CommandDescriptor {name, summary, args, permission, kind, mutating, supports_dry_run, async_capable, maps_to, handler}`,
  `ArgSpec`/`ArgType`, `ArgsView`, `CommandContext {caller, request, dry_run}`, `CommandResult {result_json, error, audit_target}`,
  `CommandRegistry::add/execute`, `CallerContext {principal, control_set, transport}`. Not yet implemented; follow the final headers.
- Verified service contract: `app::CreateLinkCommand {target_url, slug, redirect_type, expires_at,
  enabled, tags, metadata, campaign}`; errors `invalid_url`, `invalid_slug`, `reserved_slug`,
  `slug_conflict`, `invalid_field`, `storage_failure`, `internal`.
- Verified REST behavior that must be reproducible (Q18): `url` then `target_url` accepted, `slug`
  (and `code` only on the compat route), `deleted_at` rejected, response field for the target is `url`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/link_commands.hpp` | create | Declares `make_link_create_descriptor` (later subtasks add their factories). Forward-declares `app::LinkCommandService`; no storage/http includes. |
| `src/commands/link_create_command.cpp` | create | Descriptor + handler. Add to `COMMON_CPP_SOURCES`. |
| `tests/unit/commands/21_link_create_command.cpp` | create | Handler tests through a real `CommandRegistry`. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `COMMANDS_UNIT_SOURCES` += test. |

## Classes and functions

```cpp
namespace url_shortener::commands {
/// Builds the `link.create` descriptor. @p service must outlive the registry.
CommandDescriptor make_link_create_descriptor(app::LinkCommandService& service);
}
```

Descriptor fields: `name = "link.create"`; `summary = "Create a short link."`; `permission = Permission::write`;
`kind = ExecutionKind::sync`; `mutating = true`; `supports_dry_run = true` (catalog; the registry requires `mutating`);
`maps_to = {"POST /api/v1/links", "link create"}`; `args` (ArgSpec rows, exactly the catalog table):

| arg | type | required | notes |
|---|---|---|---|
| `target_url` | string | yes | |
| `slug` | string | no | generated when absent |
| `redirect_type` | enum `temporary`,`permanent` | no | default applied by the service config |
| `expires_at` | timestamp | no | RFC 3339 UTC (`Z`) |
| `enabled` | bool | no | default true |
| `tags` | list<string> (`string_list`) | no | |
| `metadata` | map<string,string> (`string_map`) | no | |
| `campaign` | object (`fields`: name, source, medium, term, content, id; all optional strings) | no | `nullable = true`: JSON `null` = no campaign |

**ArgSpec size caps (contract C3).** `ArgSpec` defaults (`max_length = 2048`, `max_items = 64`, 01.0/05) are tighter than what REST accepts
today: `normalizeTargetUrl` has no length limit (only the 65536-byte body cap applies), `validateTags` allows 20 tags of 32 characters and reports
`tags violate constraints`, `validateMetadata` allows 50 entries and reports its own message. If the registry pre-empted those checks, REST bodies
and codes would change for oversized input. Therefore every free-text arg of `link.create` (and of the other eight commands) sets
`max_length = 65536` and the list/map args set `max_items = 4096`; the registry's caps are a hard outer bound only, and
`LinkCommandService` remains the single validator with its existing messages.

Handler body (file-local, lambda or function):

```cpp
CommandResult handle_link_create(app::LinkCommandService& service,
                                 CommandContext& context, const ArgsView& args);
// 1. build app::CreateLinkCommand from args (target_url required; others only if has())
// 2. campaign: decode_campaign(args, "campaign", &cmd.campaign, &err) -> fail on error
// 3. result = context.dry_run ? service.ValidateCreateLink(cmd) : service.CreateLink(cmd);
// 4. return link_view_result(result, cmd.slug.value_or(""));   // sets audit_target
```

Decode rules mirror `createLinkFromRequest` (verified): `redirect_type` string -> `parseRedirectType`
(already constrained by the enum ArgSpec); `expires_at` string passed through (the service re-validates
and returns `invalid_field`); `enabled` only set when present; `tags`/`metadata` only when present
(absent = empty containers, as today); `campaign: null` = no campaign.

## Implementation steps

1. Create `link_commands.hpp` with the first declaration.
2. Write `link_create_command.cpp`: ArgSpec rows, descriptor assembly, handler.
3. Register the files in `sources.cmake`.
4. Tests (below) build a `CommandRegistry`, `add(make_link_create_descriptor(service))`, drive it with
   `execute(CallerContext{...}, CommandRequest{...})` over `FakeLinkStore`/`makeTestConfig()`
   (`#include "../app/app_test_fakes.hpp"`, relative include because the `commands__` target does not
   add `tests/unit/app` to its include path).
5. Build and run.

## Commands

```powershell
cmake --build cmake-build --target commands__21_link_create_command
ctest --test-dir cmake-build -R "^commands__21_link_create_command$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/21_link_create_command.cpp` | `commands__21_link_create_command` | unit | minimal args -> ok, `result` equals `app::serializeLinkViewJson` of the stored link; all optional args round-trip (tags, metadata, campaign, redirect_type, expires_at, enabled=false); `campaign:null` (no campaign stored); missing `target_url` -> `invalid_field` with message `target_url: missing`; legacy key `url` -> `invalid_field` (`url: unknown`; canonical only); `deleted_at` -> `invalid_field`; unknown key inside `campaign` -> `invalid_field`; non-https/malformed URL -> `invalid_url`; private target without `shortener_allow_private_targets` -> `invalid_url` (SSRF indistinguishable); reserved slug -> `reserved_slug`; invalid slug -> `invalid_slug`; duplicate slug -> `slug_conflict`; tag/metadata/campaign limit violations -> `invalid_field`; bad `expires_at` -> `invalid_field`; `dry_run` true -> ok result, `FakeLinkStore::findBySlug` empty afterwards; `ControlSet::User` caller -> `not_authorized` and nothing stored; forced store failure -> `storage_failure`. |

Manual check: none (parity with REST is asserted in subtask 11).

## Constraints

- C1/C3: exactly one handler; output text comes from `serializeLinkViewJson`.
- C6: error messages never echo the URL or slug; the audit line (registry) carries no arg values.
- Treat `target_url` as untrusted: no checks here beyond decoding; SSRF and URL normalization stay in
  `LinkCommandService::CreateLink` (`normalizeTargetUrl`).
- `--base-domain` / `--allow-private-targets` are CLI conveniences that mutate `ServerConfig` before the
  service is built (`parseCreateArgs`); they are **not** command args.
- C++17, no JSON library, naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `commands__21_link_create_command` passes.
2. For identical input, `execute` result text is byte-equal to `serializeLinkViewJson` of the link the
   fake store holds (test asserts string equality).
3. `dry_run` leaves the store empty; `ControlSet::User` is rejected before the handler runs.
4. `grep -n "target_url\|\"url\"" src/commands/link_create_command.cpp` shows only `target_url`.

## Out of scope / risks

- Accepting `url` as an alias in the generic endpoint was rejected (single canonical name); if a client
  needs it, add an alias facility to `ArgSpec` (01.0) rather than a special case here.
- `Link.id`/`created_at` are generated inside the service; tests compare them structurally, not by value.
