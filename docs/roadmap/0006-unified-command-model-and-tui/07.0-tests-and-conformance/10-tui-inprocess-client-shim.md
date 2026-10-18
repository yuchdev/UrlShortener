# 10 - TUI InProcessClient shim

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 03, 04; Task 06.0 (shell + `ICommandClient`/`InProcessClient`)
**Blocks:** 15, 33
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Replay the corpus through `tui::InProcessClient::call(CommandRequest)` - the
exact seam the TUI screens use (ADR 0003 rule 1) - to show the TUI's command
path adds no behavior, including the `--read-only` caller downgrade and the
"mutating commands need confirmation" bypass flag used by scripted runs.

## Dependencies

- Subtask 03 (`IConformanceAdapter`), subtask 04 (`RegistryFixture`).
- 06.0 subtask "app shell": `ICommandClient` interface and `InProcessClient` constructor (names per ADR 0003; **UNVERIFIED** whether `InProcessClient` compiles without FTXUI - ADR 0003 only guarantees the code *outside* `src/tui/` builds without it).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/support/tui_adapter.hpp` / `.cpp` | create | `TuiClientAdapter` |
| `tests/conformance/runners/02_tui_client_corpus.cpp` | create | Runner; registered only when `URLSHORTENER_ENABLE_TUI` is ON |
| `sources.cmake` | modify | `CONFORMANCE_TUI_CPP_SOURCES` (runner + adapter), separate list |
| `CMakeLists.txt` | modify | `if(URLSHORTENER_ENABLE_TUI)` loop registering `conformance__02_tui_client_corpus`, linking the TUI library target that 06.0 creates (name per 06.0) |

## Classes and functions

```cpp
namespace conformance {
class TuiClientAdapter : public IConformanceAdapter {
public:
    Transport transport() const override { return Transport::Tui; }
    TransportCaps caps() const override { return {true, false, true}; }  // no raw_request
    std::optional<std::string> beginCase(const Case&, const std::filesystem::path&) override;
    std::string send(const Step&, const std::string& request_text) override;
    void endCase() override;
private:
    std::unique_ptr<RegistryFixture> m_fixture_;
    std::unique_ptr<url_shortener::tui::InProcessClient> m_client_;   // per-role client
};
}
```

`send` constructs a `CommandRequest` from the text via the 01.0 parser, calls
`m_client_->call(request)` (the same function the form submit path calls),
serializes the `CommandResponse` with the 01.0 serializer. The role is applied
exactly as `tui --read-only` does it: a second `InProcessClient` built on a
`CallerContext` with `ControlSet::User` (do not reimplement the downgrade in
the shim - construct the client through the same factory function the `tui`
subcommand uses; **UNVERIFIED** name, see 06.0 subtask on read-only mode).

## Implementation steps

1. Reuse `RegistryFixture` (subtask 04) with transport label `"tui"`.
2. Construct clients through the production factory; assert in the first test that the factory output type is `InProcessClient`.
3. Runner mirrors subtask 04 and prints `tui: ran=N skipped=M`.
4. Link the runner only against the TUI client library and `url_shortener_common`, not the FTXUI screens, if 06.0 splits them (otherwise skip this step).

## Commands

```bash
cmake -S . -B cmake-build -G Ninja -DURLSHORTENER_ENABLE_TUI=ON
cmake --build cmake-build --target conformance__02_tui_client_corpus
ctest --test-dir cmake-build -R "^conformance__02_tui_client_corpus$" --output-on-failure
```

With `-DURLSHORTENER_ENABLE_TUI=OFF` the target does not exist and the test is not registered (no failure).

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/runners/02_tui_client_corpus.cpp` | `conformance__02_tui_client_corpus` | `integration;conformance` | whole corpus on `tui`; `raw_request` steps skipped by capability (listed in the corpus README); read-only role cases |

## Constraints

- No FTXUI rendering, no terminal; this is a client-seam test only (screens are subtask group E).
- No direct service/repository includes in the adapter beyond the fixture (fixture lives in test support, not under `src/tui/`).

## Success criteria

1. All cases pass with zero implicit skips.
2. The CI matrix runs it in the `TUI=ON` configuration; `TUI=OFF` configure still succeeds (checked in subtask 33).
3. A deliberate difference introduced in `InProcessClient` (e.g. dropping `options`) fails the runner.

## Out of scope / risks

- Latency/threading of the client (screens call it from a worker) is covered by subtask 27.
- Risk: if `InProcessClient` is entangled with FTXUI headers, split it into a header-only seam in 06.0 rather than linking FTXUI into this runner.
