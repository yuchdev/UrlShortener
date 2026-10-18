# 03 - Request envelope parser

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/01, 01.0/02
**Blocks:** 06, 04.0/09, 04.0/12, 05.0/14, 06.0/02
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Define `CommandRequest` / `CommandOptions` and `parse_request`, which turns one
request-envelope JSON text into a validated `CommandRequest` or a precise
`invalid_envelope` error. It enforces the envelope rules of
[docs/commands/README.md](/docs/commands/README.md) section 1 (version, id
length, `command` shape, `args` must be an object, `options` fields) and
generates the `srv-<n>` id when the client sends none. It does not validate
`args` contents (that is `ArgSpec`, subtask 05).

## Dependencies

- 01.0/01 `scan_object`, `find_member`, `decode_json_string`.
- 01.0/02 `CommandErrorCode::invalid_envelope`, `make_error`.
- `ServerConfig::request_id_max_length` (default 64) and
  `max_request_body_bytes` exist in `include/url_shortener/core/config.h`
  (verified); the parser takes them through an `envelope_limits` struct rather
  than including `core/config.h`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/envelope.hpp` | create | Request types, `parse_request`, `envelope_limits` (response types are added in subtask 04 to the same header) |
| `src/commands/envelope.cpp` | create | Parser and server-id generator |
| `tests/unit/commands/03_request_parse.cpp` | create | Parser tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/envelope.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {

struct CommandOptions
{
    bool dry_run = false;
    bool async = false;
};

struct CommandRequest
{
    int v = 1;
    std::string id;                  // client id or generated "srv-<n>"
    std::string command;             // dotted name, validated shape only
    std::string args_json = "{}";    // raw JSON text of the args object
    CommandOptions options;
};

struct envelope_limits
{
    std::size_t max_bytes = 65536;   // = ServerConfig::max_request_body_bytes
    std::size_t max_id_len = 64;     // = ServerConfig::request_id_max_length
    std::size_t max_command_len = 64;
    int         max_depth = 8;
};

struct parse_request_result
{
    std::optional<CommandRequest> request;
    CommandError error;              // set when !request
    std::string id;                  // best-effort id for the error response
    bool ok() const { return request.has_value(); }
};

parse_request_result parse_request(std::string_view text,
                                   const envelope_limits& limits = {});

std::string next_server_id();        // "srv-<n>", process-wide std::atomic<uint64_t>
bool is_valid_command_name(std::string_view name);   // ^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$ , no regex
}
```

Rules enforced by `parse_request` (each failure returns
`CommandErrorCode::invalid_envelope`, with a fixed message naming the field,
never echoing values):

- text is one JSON object (scanner errors map to messages `malformed`,
  `too_large`, `too_deep`, `duplicate_key`);
- `v` present, an integer token, equal to `1` (`unsupported version` is still
  `invalid_envelope` per the catalog row "bad `v`");
- `command` present, a string, `is_valid_command_name`, length <=
  `max_command_len`;
- `id` optional, a string, 1..`max_id_len` characters, no control characters
  (so ids are safe to log and to echo); absent -> `next_server_id()`;
- `args` optional; when present must be an object (otherwise `invalid_envelope`);
  absent -> `"{}"`; stored verbatim as `args_json`;
- `options` optional object; `dry_run` / `async` optional booleans;
  unknown keys inside `options` -> `invalid_envelope` (the catalog fixes the
  two fields);
- unknown top-level keys -> `invalid_envelope` (envelope is closed in v1;
  additive changes do not bump `v`, so a newer client sending a new top-level
  key to an older server is rejected explicitly, not ignored).

Open question carried to the decision log (not decided here): rejecting
unknown top-level keys follows the strictness of "unknown keys are rejected
(`invalid_field`)" for `args`, but the catalog is silent for the envelope
itself. The conservative choice (reject) is implemented; relaxing it is a
one-line change.

## Implementation steps

1. Add `envelope.hpp` with the request side only (subtask 04 appends the
   response side to the same header).
2. Implement `is_valid_command_name` by hand (no `<regex>`).
3. Implement `next_server_id` with a function-local `std::atomic<uint64_t>`.
4. Implement `parse_request` on top of `scan_object`; extract `id` first so the
   error response can still echo a valid client id when another field is bad
   (`parse_request_result::id`, falls back to `srv-<n>`).
5. Convert the integer token for `v` and `options` booleans without
   `extractJsonBoolField` (use the scanner's `kind` and `raw`).
6. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__03_request_parse
ctest --test-dir cmake-build -R "^commands__03_request_parse$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/03_request_parse.cpp` | `commands__03_request_parse` | unit | below |

- Round-trips every request example in docs/commands/README.md section 1 and
  the worked example in section 2 (`link.create` with a nested `args`).
- Minimal envelope `{"v":1,"command":"command.list"}`: `id` starts with
  `srv-`, `args_json == "{}"`, options default false.
- `id` echoed unchanged; 64 chars accepted, 65 rejected; empty string and
  control character rejected.
- Missing `command`; `command` not a string; names `Link.Create`, `link`,
  `link..x`, `.x`, 65-char name -> `invalid_envelope`.
- `v` missing, `0`, `2`, `"1"`, `1.0`, `true` -> `invalid_envelope`.
- `args` as array/string/number/null -> `invalid_envelope`; `args` with nested
  object preserved byte-for-byte in `args_json`.
- `options`: both flags true; wrong type (`"dry_run":"yes"`); unknown option key.
- Unknown top-level key (`"foo":1`) rejected.
- Non-JSON, empty text, array, 70 KB text (with default limits) rejected; error
  `id` is the client id when one could be read, else `srv-...`.
- `next_server_id` is monotonic across 1000 calls and unique across 4 threads.

## Constraints

- Closed-envelope strictness and fixed error messages (no echo of input) to
  avoid log injection; ids are length-bounded and control-char-free
  (`request_id_max_length` is the existing hardening limit, CLAUDE.md).
- No JSON library; only `json_scan.hpp`.
- Milestone contracts: C2 (envelope is the only wire shape), C9.
- Header must not include `core/config.h` (keeps `commands/` free of server
  config coupling; limits are passed in).

## Success criteria

1. `commands__03_request_parse` passes.
2. Every request example in the catalog parses (test iterates a table copied
   from the doc).
3. No error message contains any substring of the input (test feeds a request
   with a recognizable secret in `args` and a bad `v`, then asserts the message
   does not contain the secret).

## Out of scope / risks

- `args` validation (05); response envelope (04); HTTP-level body size limit
  (the REST adapter enforces `max_request_body_bytes` before calling; the
  parser's own `max_bytes` is defense in depth).
- Server-generated ids are not unguessable and carry no meaning (ADR: `id`
  never carries meaning for the server).
