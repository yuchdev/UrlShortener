# 08 - Command API configuration gate

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 01.0/06 (`CallerContext`)
**Blocks:** 07, 09, 10, 05.0/11
**Estimated size:** S
**Suggested agent:** cpp-expert (security-auditor reviews the default)

## Objective

The generic `POST /api/v1/commands` / `GET /api/v1/commands` endpoints expose **every** registered command
(`db.rows`, `system.info`, import/export, jobs, and later auth commands) over HTTP, and the new `GET /api/v1/links`
(subtask 07) and `/api/v1/jobs*` (Task 05.0 subtask 11) routes expose bulk link data and job results, but this server
has no authentication in front of its REST routes (verified: `src/http` never constructs an `AccessGuard`). Until
Milestone 0002 supplies sessions, **all of this new surface** must be opt-in and run with an explicit, least-privilege
role. This subtask adds the two configuration switches and the one helper every gated route uses
(`command_api_caller`), and nothing else.

## Dependencies

- Verified `ServerConfig` (`include/url_shortener/core/config.h`): flat struct of defaults; booleans are parsed from CLI strings by `parseBool`
  (`src/cli_parser.cpp`, e.g. `shortener-allow-private-targets`); options are declared in `CliParser::parse` (Boost.ProgramOptions groups
  `shortener_opts`, `analytics_opts`, `request_opts`) and listed in `--help`.
- `ControlSet` (global enum in `security/control_set.hpp`): `Admin`, `User`.
- Decision recorded here (flag for review): the generic endpoint is **off by default**; when on, its caller role defaults to `user`
  (read commands only). The legacy resource routes are unaffected (they keep today's open behavior, subtask 02).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/core/config.h` | modify | Add `bool command_api_enabled = false;` and `std::string command_api_role = "user";` to `ServerConfig`. |
| `src/cli_parser.cpp` | modify | Declare `--command-api-enabled <BOOL>` and `--command-api-role <user\|admin>` in a new "Command API options" group; validate the role; include in `--help`. |
| `tests/unit/cli/11_cli_parser_command_api_flags.cpp` | create | Flag parsing and validation. |
| `sources.cmake` | modify | `CLI_UNIT_SOURCES` += the test (target `cli__11_cli_parser_command_api_flags`); `HTTP_UNIT_SOURCES` += `tests/unit/http/24_command_api_caller.cpp`. |
| `include/url_shortener/http/command_bridge.hpp`, `src/http/command_bridge.cpp` | modify | Add the helpers below (the files are created by subtask 02; if this subtask lands first it creates them with only these helpers). |

## Classes and functions

```cpp
// ServerConfig additions
bool command_api_enabled = false;          ///< Enables POST/GET /api/v1/commands (off by default).
std::string command_api_role = "user";     ///< Role used for generic-endpoint callers: "user" (read-only) or "admin".
```

Parser rules: `--command-api-enabled` uses the existing `parseBool` (accepts what the other BOOL flags accept; invalid -> `std::invalid_argument`
with the flag name); `--command-api-role` accepts exactly `user` or `admin` (case-sensitive) else `std::invalid_argument("--command-api-role must be user or admin")`.
Neither option is read from the environment (a secure-by-default switch should be an explicit argument, and the help text says so). The role value is
not a secret; it is logged at startup as `command_api=enabled role=user` by the server's existing startup logger call sites (one added `logger.info` field
pair in `main.cpp` is optional and **not** part of this subtask).

Helpers (declared in `http/command_bridge.hpp`, implemented in `src/http/command_bridge.cpp`, subtask 02's file, so
`core/config.h` does not include `security/control_set.hpp`):

```cpp
/// Configured role, before the peer check.
ControlSet command_api_control_set(const ServerConfig& config);   // "admin" -> Admin, else User
/// True for 127.0.0.0/8 and ::1 (also IPv4-mapped ::ffff:127.x.y.z).
bool is_loopback_peer(const boost::asio::ip::address& peer);
/// The one caller used by every gated route (generic endpoint, GET /api/v1/links, /api/v1/jobs*):
/// principal "command-api", transport "rest", control set = Admin only when the configured role is admin
/// AND the peer is loopback; otherwise User. Listeners bind all IPv4 interfaces (tcp::v4() in
/// http_server.cpp, verified), so a bind-address check is impossible; the peer check is the guard.
commands::CallerContext command_api_caller(const ServerConfig& config, const boost::asio::ip::address& peer);
/// The one gate check: false -> the route answers exactly like an unmatched route.
bool command_api_enabled(const ServerConfig& config);
```

The peer address reaches the handler through the request context; the exact accessor (the session already logs
`remote_addr`) is **UNVERIFIED** and is resolved in subtask 09 (add it to `RouteContext` if absent).

## Implementation steps

1. Add the two fields with Doxygen comments.
2. Add the options and validation in `CliParser::parse`; extend the help group.
3. Write the unit test; register it in `CLI_UNIT_SOURCES`.
4. Run all CLI unit tests to prove `link` parsing and server-flag parsing are unchanged.

## Commands

```powershell
cmake --build cmake-build --target cli__11_cli_parser_command_api_flags cli__01_cli_parser_link_commands
ctest --test-dir cmake-build -R "^cli__(0[1-4]|11)_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/11_cli_parser_command_api_flags.cpp` | `cli__11_cli_parser_command_api_flags` | unit | defaults: `command_api_enabled == false`, `command_api_role == "user"`; `--command-api-enabled true` / `false` / `1` handled like other BOOL flags; invalid bool -> `std::invalid_argument`; `--command-api-role admin` and `user` accepted; `Admin`, `root`, empty -> `std::invalid_argument`; the flags combine with `--http-port 9000`; they do not disturb the `link <verb>` and `cmd` short-circuits; `--help` text lists both flags and states the default is off, and that `admin` applies to loopback peers only. |
| `tests/unit/http/24_command_api_caller.cpp` | `24_command_api_caller` | unit | `command_api_caller`: role `user` + loopback -> `User`; role `admin` + `127.0.0.1` / `::1` / `::ffff:127.0.0.2` -> `Admin`; role `admin` + `10.0.0.5` / `192.168.1.2` / `::ffff:10.0.0.1` -> `User`; principal and transport fixed; `command_api_enabled` mirrors the flag. |

Manual check: `url_shortener --help` shows the group.

## Constraints

- Secure by default: the default must stay `false` / `user`; changing it requires the security-auditor sign-off recorded in the PR.
- No secret or credential is introduced; the role is not a credential.
- C3/C4: no existing flag or default changes; server-mode parsing otherwise byte-identical.
- C++17; naming per `.clang-tidy`; no new `.cpp`.

## Success criteria

1. `cli__11_cli_parser_command_api_flags` passes; `cli__01..04` pass unchanged.
2. `ServerConfig{}.command_api_enabled == false` (asserted).
3. `url_shortener --help | grep command-api` shows two lines.

## Out of scope / risks

- Real authentication for the generic endpoint (Milestone 0002 C2): when it lands, `command_api_role` becomes the fallback for unauthenticated callers or is removed.
- Enabling the endpoint exposes `db.rows`, `system.info`, `GET /api/v1/links` and job results to anyone who can reach
  the port (read-only unless the peer is loopback and the role is `admin`); the operator guide (Task 08.0 subtask 07)
  must say so.
