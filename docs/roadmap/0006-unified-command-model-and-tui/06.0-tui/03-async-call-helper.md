# 03 - Async call helper (worker + PostEvent)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/01, 06.0/02
**Blocks:** 04, 06, 08, 09, 11, 12, 13, 14, 15, 16, 17, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement ADR 0003 rule 3 ("never block the UI thread"): run every
`ICommandClient::call` on a worker thread and deliver the response back on the
UI thread through FTXUI's thread-safe posting. The logic is a pure class
(`AsyncCaller`) over an injected `IUiPoster`, so it is unit-tested without a
terminal; a 15-line adapter binds the poster to `ftxui::ScreenInteractive`.
Renders no diagram; it is the asynchronous hop in **D12** between "Screen" and
"ICommandClient" and feeds **D8** polling (subtask 16).

## Dependencies

- Internal: 06.0/02 (`ICommandClient`, `MockCommandClient`).
- External: FTXUI only in the adapter: `ftxui::ScreenInteractive::Post(std::function<void()>)` and/or `PostEvent(ftxui::Event)` (both documented as thread-safe; which of `Post`/`Event::Custom` exists at the pinned version is **VERIFY AT PIN TIME**). `std::thread`, `std::mutex`, `std::condition_variable`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/async_caller.hpp` | create | `IUiPoster`, `AsyncCaller`, `CallToken`, `RequestIdGenerator` |
| `src/tui/async_caller.cpp` | create | worker pool + completion marshalling (model lib) |
| `include/url_shortener/tui/ftxui_poster.hpp` | create | `FtxuiPoster : IUiPoster` |
| `src/tui/ftxui_poster.cpp` | create | binds to `ScreenInteractive&` (UI lib) |
| `tests/unit/tui/support/inline_poster.hpp` | create | `InlinePoster`: queue drained by the test (`run_pending()`), simulates the UI thread |
| `tests/unit/tui/03_async_caller_test.cpp` | create | see Tests |
| `sources.cmake` | modify | add `async_caller.cpp` to `TUI_MODEL_CPP_SOURCES`, `ftxui_poster.cpp` to `TUI_UI_CPP_SOURCES`, test to `TUI_MODEL_UNIT_SOURCES` |

## Classes and functions

Namespace `url_shortener::tui` (all NEW):

```cpp
class IUiPoster {
public:
    virtual ~IUiPoster() = default;
    virtual void post(std::function<void()> task) = 0;   // thread-safe; runs task on the UI thread
};

using CallToken = std::uint64_t;
using ResponseHandler = std::function<void(const commands::CommandResponse&)>;

class AsyncCaller {
public:
    AsyncCaller(ICommandClient& client, IUiPoster& poster, std::size_t workers = 2);
    ~AsyncCaller();                                  // stops workers, joins; pending handlers never run after this returns
    CallToken submit(commands::CommandRequest request, ResponseHandler on_done);
    void cancel(CallToken token);                    // result discarded; the call itself is not interrupted
    std::size_t in_flight() const;
    bool busy() const { return in_flight() > 0; }
private:
    // m_client, m_poster, m_queue, m_mutex, m_cv, m_threads, m_cancelled, m_next_token, m_stopping
};

class RequestIdGenerator {                           // "tui-1", "tui-2"...; thread-safe; stays <= 64 chars
public:
    std::string next();
};

class FtxuiPoster final : public IUiPoster {         // UI lib
public:
    explicit FtxuiPoster(ftxui::ScreenInteractive& screen);
    void post(std::function<void()> task) override;  // screen.Post(std::move(task)) -- VERIFY AT PIN TIME
};
```

Rule for screens: handlers passed to `submit` run on the UI thread, may touch
components/state, and must trigger a redraw (`ScreenInteractive::PostEvent(Event::Custom)`
inside `FtxuiPoster::post` guarantees one frame after each task).

## Implementation steps

1. Bounded FIFO queue (cap 64); `submit` beyond the cap invokes the handler immediately via the poster with a synthesized `{"ok":false,"error":{"code":"busy",...}}` envelope (reuse the envelope builder from Task 01.0 - UNVERIFIED name) so the UI never blocks or silently drops.
2. Worker loop: pop, `client.call`, then `poster.post([=]{ if (!cancelled(token)) handler(resp); })`; exceptions from `call` become an `internal` error envelope.
3. Destructor: set `m_stopping`, notify, join; completions already posted but not yet run are guarded by a `std::shared_ptr<std::atomic<bool>> m_alive` captured in each task so they are no-ops after destruction.
4. `cancel`: add token to a set; completion checks and erases it.
5. `FtxuiPoster::post` wraps the task so that after it runs a repaint is requested.
6. Document the threading contract in the header: `ICommandClient` implementations must be callable from worker threads; `InProcessClient` is, because `CommandRegistry::execute` is const (verify in Task 01.0 that handlers are thread-safe against the shared services; if not, set `workers = 1`).

## Commands

```powershell
cmake --build cmake-build --target tui__03_async_caller_test
ctest --test-dir cmake-build -R "^tui__03_" --output-on-failure
# stress: run 50 times to shake out races (PowerShell)
1..50 | ForEach-Object { ctest --test-dir cmake-build -R "^tui__03_" --output-on-failure }
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `03_async_caller_test.cpp` | `tui__03_async_caller_test` | unit | handler runs only after `InlinePoster::run_pending()` (never on the worker thread - assert thread id differs from the worker's and equals the test thread); responses delivered per token with out-of-order completion (mock with per-call delays); `cancel` suppresses handler; destructor with in-flight call does not run handler and does not hang; `submit` over capacity yields `busy` envelope; exception in client -> `internal` envelope; `in_flight` returns to 0; `RequestIdGenerator` unique across 4 threads x 1000 ids and <= 64 chars; 1,000 submits stress |

Manual: covered by the freeze check in [20](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/20-manual-smoke-checklist.md) (a slow `link.list` must not stop key handling).

## Constraints

- Rule 3 of ADR 0003: no `ICommandClient::call` on the UI thread, ever (enforced by convention + the boundary test scanning `screens/` for direct `->call(` uses: add that regex to subtask 02's test when this lands).
- C8: `AsyncCaller` depends on `ICommandClient`, not the registry.
- C9: standard library threading only; no new dependency.
- Handlers must not capture raw pointers to screens that can be destroyed; use `std::weak_ptr` or tokens (see Step 3 guard).

## Success criteria

1. Under `InlinePoster`, no handler ever executes before `run_pending()`.
2. 50 consecutive runs of the test are green on Windows and one POSIX platform (ThreadSanitizer clean where available).
3. `submit` never blocks the caller (measured: 1,000 submits against a 10 ms mock complete in < 100 ms of caller time).
4. Destruction during in-flight work completes within 2 s.

## Out of scope / risks

- True cancellation of a running command (jobs have `job.cancel` instead).
- Risk: `ScreenInteractive::Post` availability/semantics differ by FTXUI version; fall back to a mutex-protected task queue drained when a posted `Event::Custom` arrives.
