# 22 - TUI mock command client helpers (over the 06.0 mock)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 06.0 (`ICommandClient`, the final `MockCommandClient` of subtask 02, `register_tui_tests` and the TUI test lists of subtask 01)
**Blocks:** 23, 26, 27, 28, 29, 08.0/11
**Estimated size:** S
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** The 06.0 `MockCommandClient`
> is **shipped and `final`** - it cannot be extended in place, and its 57 users
> fix its API. It already provides `enqueue(command, CommandResponse)` (FIFO per
> command), `set_commands`, `set_session`, `set_delay` (**not** `set_latency`),
> `set_permission_hook`, `recorded_requests()` (thread-safe copy), the public
> `requests` vector and `required_permission` - recording **`CommandRequest`
> structs, not text**, and returning an `internal` error (not `unknown_command`)
> for an unscripted call. So this subtask no longer modifies the mock. The only
> gap is a small free-function **helper header** `tui_test_support.hpp`
> (envelope builders, a real-registry `command.list` fixture, and request-text
> serialization helpers) plus a self-test. Removed APIs that do not exist:
> `on_command`, `set_default`, `fail_next`, `set_latency`, `request_texts`/
> `request_texts_for` (as mock methods), `expect_no_calls`, `clear`.

## Objective

Give the 07.0 TUI tests the ergonomics they need **without touching the final
06.0 mock**: envelope builders, a `command.list` fixture built from the real
registry, and helpers that serialize the mock's recorded `CommandRequest`
structs into byte-exact envelope text for the "exact envelope" assertions
(ADR 0003 rule 6). All as free functions over the existing mock.

There is exactly **one** mock class and **one** TUI test registration scheme
(06.0 README K3/K5): this subtask adds no second mock, no `TUI_UNIT_SOURCES`
list and no CMake loop.

## Coverage already delivered (06.0)

| Need | Delivered by (consumed) |
|---|---|
| recording mock with scripting, delay, permission hook | `tests/unit/tui/support/mock_command_client.hpp` (`MockCommandClient`, final) |
| its own exercise | `tui__02_in_process_client_test` and every `tui__04`..`tui__21` screen test use it |

## Dependencies

- 06.0/02: the final `MockCommandClient` (API above) and `json_view`.
- 06.0/01: `TUI_MODEL_UNIT_SOURCES` / `TUI_RENDER_UNIT_SOURCES` and `register_tui_tests` (targets `tui__<stem>`, only under `URLSHORTENER_ENABLE_TUI`).
- Task 01.0 serializer + Task 03.0 `composition::build_command_runtime` for the real-descriptor fixture.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/support/tui_test_support.hpp` | create | free-function helpers only; no new mock class |
| `tests/unit/tui/22_mock_support_helpers_test.cpp` | create | self-test (stem `22` is free under `tests/unit/tui/`) |
| `sources.cmake` | modify | append the test to `TUI_MODEL_UNIT_SOURCES` (owned by 06.0/01; no new list) |

## Classes and functions

```cpp
namespace url_shortener::tui::testing {   // same namespace as the 06.0 mock

// Envelope builders - return a commands::CommandResponse ready for mock.enqueue().
commands::CommandResponse ok_envelope(std::string_view id,
                                      std::string_view result_json);
commands::CommandResponse err_envelope(std::string_view id,
                                       std::string_view code);

// command.list response built from a real build_command_runtime registry over
// the in-memory backend and the 01.0 serializer, so form/palette tests track
// real ArgSpec changes. Returns the response the TUI's commands() path consumes.
commands::CommandResponse command_list_fixture();
std::vector<CommandInfo>  command_list_infos();   // the same, as CommandInfo for set_commands()

// Request-text helpers over the mock's recorded CommandRequest structs
// (the mock records structs, not text): serialize to byte-exact envelope text.
std::string request_text(const commands::CommandRequest&);              // 01.0 serializer
std::vector<std::string> request_texts(const MockCommandClient&);       // all, in call order
std::vector<std::string> request_texts_for(const MockCommandClient&,
                                            std::string_view command);
void expect_no_calls(const MockCommandClient&);                         // BOOST_FAIL if any recorded
}
```

These are the renamed, as-built forms of the originally-specced `okEnvelope`/
`errEnvelope`/`commandListFixture`/`request_texts_for`/`expect_no_calls`, moved
off the (final) mock and onto free functions that read `recorded_requests()`.
Failure injection uses `enqueue(command, err_envelope(...))`; "default unknown"
is the mock's built-in `internal` error for an unscripted call.

## Implementation steps

1. Write `tui_test_support.hpp` (header-only, no FTXUI include so model tests link without the UI library).
2. Self-test: `ok_envelope`/`err_envelope` round-trip; `command_list_fixture` contains the real `link.create` descriptor; `request_texts`/`request_texts_for` serialize the recorded structs byte-exactly; `expect_no_calls` fires on an empty mock and fails on a used one.
3. Append to `TUI_MODEL_UNIT_SOURCES`; verify `-DURLSHORTENER_ENABLE_TUI=OFF` configure ignores the list without error.

## Commands

```bash
cmake -S . -B cmake-build -G Ninja -DURLSHORTENER_ENABLE_TUI=ON
cmake --build cmake-build --target tui__22_mock_support_helpers_test
ctest --test-dir cmake-build -R "^tui__22_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/22_mock_support_helpers_test.cpp` | `tui__22_mock_support_helpers_test` | `unit` | 4 |

## Constraints

- No FTXUI include in `tui_test_support.hpp` (model tests link without the UI library).
- Recorded requests are serialized to byte-exact text for the "exact envelope" assertions; tests compare strings where the plan demands "exact envelope".
- The helpers talk to a registry only in `command_list_fixture()`/`command_list_infos()`.
- Naming per `.clang-tidy` (`lower_case`).

## Success criteria

1. Self-test passes (advisory under TSan if available).
2. Subtasks 23-28 need no additional mock features beyond the existing mock + these helpers (checked by review).
3. `TUI=OFF` build is unaffected; there is exactly one `MockCommandClient` in the tree (grep); the final 06.0 mock is unmodified.

## Out of scope / risks

- Risk: if `ICommandClient` becomes callback-based, the helpers wrap the deterministic `InlinePoster` (06.0/03) drain, which already exists.
