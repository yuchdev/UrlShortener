# 22 - TUI mock command client (extension of the 06.0 mock)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Task 06.0 (`ICommandClient` and the base `MockCommandClient` of subtask 02, `register_tui_tests` and the TUI test lists of subtask 01)
**Blocks:** 23, 26, 27, 28, 29, 08.0/11
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Extend the **existing** recording `MockCommandClient` that Task 06.0 subtask 02
creates (`tests/unit/tui/support/mock_command_client.hpp`) with the scripting
and assertion helpers the 07.0 TUI tests need (per-command responders, failure
injection, latency, byte-exact request recording, a `command.list` fixture
built from the real descriptors), so every TUI test (snapshots, forms, state
machine, confirm dialogs) runs headless with exact control over responses and
can assert the exact envelope bytes a screen sent (ADR 0003 rule 6).

There is exactly **one** mock class and **one** TUI test registration scheme
(06.0 README K3/K5, plan.md "Test file prefixes"): this subtask adds no second
mock, no `TUI_UNIT_SOURCES` list and no CMake loop.

## Dependencies

- 06.0/02: `ICommandClient` (`call(CommandRequest) -> CommandResponse`, `commands()`, `session()`), the base `MockCommandClient`
  (`enqueue`, `requests`, per-call delay, `set_session`, `set_commands`) and `json_view`.
- 06.0/01: `TUI_MODEL_UNIT_SOURCES` / `TUI_RENDER_UNIT_SOURCES` and the `register_tui_tests` macro (targets `tui__<stem>`, only under
  `URLSHORTENER_ENABLE_TUI`).
- Task 01.0: envelope serializer (`serialize_response`, 01.0/04) and descriptor types for `commandListFixture()`; the real
  descriptors come from `composition::build_command_runtime` (03.0/18) with stub-free in-memory wiring.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/support/mock_command_client.hpp` | modify | add the scripting/recording API below to the 06.0 class (header-only, additive; existing 06.0 tests keep compiling) |
| `tests/unit/tui/support/tui_test_support.hpp` | create | helpers: `okEnvelope(id, result_json)`, `errEnvelope(id, code)`, `commandListFixture()` |
| `tests/unit/tui/23_mock_command_client_test.cpp` | create | self-test (Task 07.0 owns `tests/unit/tui/22-69`) |
| `sources.cmake` | modify | append the test to `TUI_MODEL_UNIT_SOURCES` (owned by 06.0/01; no new list) |

## Classes and functions

```cpp
namespace url_shortener::tui::testing {   // namespace of the 06.0 mock (06.0/02)

class MockCommandClient : public url_shortener::tui::ICommandClient {   // 06.0/02 class, extended
public:
    using Responder = std::function<std::string(const std::string& request_text)>;  // returns response envelope text

    // Scripting (added here)
    void on_command(std::string_view command, Responder);                // exact command name
    void on_command(std::string_view command, std::string response_text); // constant
    void set_default(Responder);                                         // unmatched commands (default: unknown_command envelope)
    void set_latency(std::chrono::milliseconds);                          // for non-blocking tests (subtask 27); reuses 06.0's delay
    void fail_next(std::string_view command, std::string_view error_code);

    // Recording (added here; 06.0's `requests` stays)
    const std::vector<std::string>& request_texts() const;               // serialized request envelopes, in call order
    std::vector<std::string> request_texts_for(std::string_view command) const;
    void expect_no_calls() const;                                        // BOOST_FAIL if any recorded
    void clear();
    // ICommandClient overrides unchanged (06.0/02)
};

std::string okEnvelope(std::string_view id, std::string_view result_json);
std::string errEnvelope(std::string_view id, std::string_view code);
std::string commandListFixture();    // full descriptor list, built from the real registry (not hand-written) so form tests track ArgSpec changes
}
```

`commandListFixture()` builds the `command.list` response by calling a real
`build_command_runtime` registry over the in-memory backend and the 01.0
serializer - the TUI must see exactly what production sends.

## Implementation steps

1. Extend the header-only mock additively; keep it thread-safe (screens call from a worker thread).
2. Self-test: scripted response, default unknown, recording order, `fail_next`, thread-safety (8 threads x 100 calls), `expect_no_calls`, and that the 06.0/02 API (`enqueue`, `requests`) still behaves as before.
3. Append to `TUI_MODEL_UNIT_SOURCES`; verify `-DURLSHORTENER_ENABLE_TUI=OFF` configure ignores the list without error.

## Commands

```bash
cmake -S . -B cmake-build -G Ninja -DURLSHORTENER_ENABLE_TUI=ON
cmake --build cmake-build --target tui__23_mock_command_client_test
ctest --test-dir cmake-build -R "^tui__23_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/23_mock_command_client_test.cpp` | `tui__23_mock_command_client_test` | `unit` | 7 (6 above + 06.0 API regression) |

## Constraints

- No FTXUI include in `mock_command_client.hpp` (model tests link without the UI library).
- Recorded requests are the serializer output, byte-exact; tests compare strings, not parsed structures, where the plan demands "exact envelope".
- The mock never talks to a registry except in `commandListFixture()`.
- Naming per `.clang-tidy` (`lower_case` methods).

## Success criteria

1. Self-test passes under ThreadSanitizer/`-fsanitize=thread` on Linux CI if available (advisory).
2. Subtasks 23-28 need no additional mock features beyond this interface (checked by review).
3. `TUI=OFF` build is unaffected; there is exactly one `MockCommandClient` in the tree (grep).

## Out of scope / risks

- Risk: if `ICommandClient` becomes callback-based, wrap the callback in the mock with a deterministic executor and expose `drain()`.
