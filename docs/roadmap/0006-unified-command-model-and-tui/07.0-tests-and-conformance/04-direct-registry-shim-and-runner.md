# 04 - Direct registry shim and runner

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 03; Tasks 01.0, 02.0, 03.0
**Blocks:** 10, 11, 12, 13, 14, 33
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Implement the reference adapter: replay the corpus straight into
`CommandRegistry::execute` with no transport in between. Every other adapter is
judged against the same expectations, and this one is the fastest and the first
to be run when authoring corpus cases.

## Dependencies

- Subtask 03 (`IConformanceAdapter`, `CaseRunner`).
- Task 01.0: `CommandRegistry::execute(const CallerContext&, const CommandRequest&)`, envelope parse/serialize, `CallerContext{principal, ControlSet, transport}`.
- Task 02.0 / 03.0: the composition entry point that builds the registry plus services over a configured backend (the same one `main` uses). **UNVERIFIED:** its final name; the shim wraps it in one function so only one line changes.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/support/registry_fixture.hpp` / `.cpp` | create | `RegistryFixture`: temp-dir SQLite or in-memory backend, full registry, job runner |
| `tests/conformance/support/direct_adapter.hpp` / `.cpp` | create | `DirectRegistryAdapter` |
| `tests/conformance/runners/01_direct_registry_corpus.cpp` | create | Boost.Test runner over the full corpus |
| `sources.cmake` | modify | Add the two `.cpp` files to `CONFORMANCE_SUPPORT_CPP`, runner to `CONFORMANCE_CPP_SOURCES` |

## Classes and functions

```cpp
namespace conformance {
class RegistryFixture {
public:
    RegistryFixture(const std::filesystem::path& tmp, std::string_view backend /*"sqlite"|"inmemory"*/);
    ~RegistryFixture();                           // stops job runner, closes DB
    url_shortener::commands::CommandRegistry& registry();
    url_shortener::commands::CallerContext caller(std::string_view role) const; // admin|user, transport="direct"
};

class DirectRegistryAdapter : public IConformanceAdapter {
public:
    Transport transport() const override { return Transport::Direct; }
    TransportCaps caps() const override { return {true, true, true}; }
    std::optional<std::string> beginCase(const Case&, const std::filesystem::path&) override;
    std::string send(const Step&, const std::string& request_text) override;
    void endCase() override;
private:
    std::unique_ptr<RegistryFixture> m_fixture_;
};
}
```

`send` parses `request_text` with the 01.0 envelope parser; on parse failure it
returns the 01.0 `invalid_envelope` response serialization (the same helper
every other transport must call), otherwise `registry.execute(caller(step.caller), req)` and serializes the response with the 01.0 serializer. The adapter never builds a response itself.

## Implementation steps

1. Implement `RegistryFixture` using a fresh `${tmp}/links.db` (sqlite) per case; `inmemory` uses the in-memory adapter. Base domain fixed to `sho.rt` so `short_url` is deterministic; generated slug length left at default (matchers use `<slug:N>`).
2. Fixed clock: inject `ManualClock` (`core/clock.hpp`) only if 01.0/03.0 expose the injection point; otherwise rely on `<ts>` matchers. **Do not** make corpus expectations depend on `ManualClock` - other transports cannot use it.
3. Runner: `CorpusLoader::loadDirectory(CONFORMANCE_CORPUS_DIR "/commands")`, `CaseRunner::run`, `BOOST_TEST(report.failed == 0)` with failures printed via `BOOST_TEST_MESSAGE`; assert `report.ran > 0` so an empty corpus cannot pass.
4. Print a one-line summary `direct: ran=N skipped=M` for CI logs.

## Commands

```bash
cmake --build cmake-build --target conformance__01_direct_registry_corpus
ctest --test-dir cmake-build -R "^conformance__01_direct_registry_corpus$" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/runners/01_direct_registry_corpus.cpp` | `conformance__01_direct_registry_corpus` | `integration;conformance` | whole corpus on `direct`; `ran > 0`; skip count equals number of `transports.skip` entries naming `direct` (expected 0) |

## Constraints

- The shim contains zero command knowledge (no switch on command names).
- Each case gets a fresh store and fresh job runner; no state leaks between cases.
- A case that needs more than 5 s fails (job polling timeout is the only place waiting is allowed).

## Success criteria

1. With the 11-14 corpus present, `direct` runs every case with 0 skips.
2. Running the runner twice in the same process (repeat test) gives identical results (no leaked state).
3. Introducing a deliberate bug in one handler makes exactly the matching cases fail with a readable path (verified once by hand, recorded in the PR).

## Out of scope / risks

- Role assertions (`user`) run through `CallerContext`; REST role derivation is subtask 08's problem.
- Risk: fixture construction cost per case with SQLite; if the full corpus exceeds 20 s, use one DB file per suite and truncate between cases (do not share across cases without truncation).
