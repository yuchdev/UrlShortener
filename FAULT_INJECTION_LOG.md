# Fault Injection Log — Aegis SWR Forensic Analyzer

**Branch:** `feature/fault-injection-aegis`  
**Target file:** `src/analytics/AnalyticsWorker.cpp`  
**Bug archetype:** Asynchronous Lambda Reference Capture (Use-After-Free)  
**Severity:** Fatal — produces `SIGSEGV` (or heap corruption leading to `std::terminate`)

---

## 1. Injection Location

**File:** `src/analytics/AnalyticsWorker.cpp`  
**Function:** `AnalyticsWorker::Start()`

```cpp
void AnalyticsWorker::Start()
{
    if (!config_.enabled || state_ == AnalyticsWorkerState::Running) return;
    state_ = AnalyticsWorkerState::Running;

    // One-shot startup probe: after the first flush cycle completes, record the
    // initial queue depth keyed by the runtime configuration salt for diagnostics.
    const std::string probe_label =
        "analytics:probe:" + config_.client_hash_salt + ":init";   // ← local, stack-lifetime

    std::thread([this, &probe_label]() {                            // ← captured by REFERENCE
        std::this_thread::sleep_for(config_.flush_interval);
        metrics_.SetQueueDepth(static_cast<std::uint64_t>(probe_label.size())); // ← UAF here
    }).detach();                                                     // ← thread outlives Start()

    thread_ = std::thread([this] { Run(); });
}
```

---

## 2. Root-Cause Analysis

### What goes wrong

`probe_label` is a `std::string` local variable with **automatic (stack) storage duration** in
`AnalyticsWorker::Start()`. Its lifetime ends when `Start()` returns.

The lambda **captures `probe_label` by reference** (`[this, &probe_label]`). The `std::thread` is
immediately `.detach()`-ed, making it fully independent of `Start()`'s call frame.

**Execution timeline:**

```
T+0ms   Start() called
T+0ms   probe_label constructed on Start()'s stack frame
                "analytics:probe:<salt>:init"  → heap-allocated string (≥16 chars in libstdc++)
T+0ms   detached thread spawned, holds &probe_label (dangling once Start() returns)
T+0ms   Start() returns → probe_label DESTROYED, heap buffer freed
T+Nms   detached thread wakes up after flush_interval (default 1 000 ms)
T+Nms   probe_label.size() called on the destroyed string object
            → reads _M_string_length from the (now-reused) stack frame
            → if non-SSO, _M_p points to freed heap memory; any read/write is UB
T+Nms   SIGSEGV / heap-use-after-free / memory corruption
```

### Why it is elusive

| Property | Detail |
|---|---|
| **Delayed trigger** | Crash occurs ≈ `flush_interval` (default 1 s) after `Start()` is called, not at startup. |
| **Race-dependent** | If the stack frame of `Start()` is not reused before the thread wakes up, the corrupted read may silently return a stale (but non-trapping) value, deferring the crash further or masking it entirely. |
| **Salt-dependent** | `probe_label` overflows SSO only when `client_hash_salt` is non-empty. With an empty salt the string `"analytics:probe::init"` is 21 characters — still above the libstdc++ SSO threshold of 15, so the heap path is always taken in practice. |
| **Platform variance** | On systems with aggressive heap poisoning (ASan, glibc `MALLOC_PERTURB_`) the crash is immediate and deterministic; on production builds without those guards it is intermittent. |
| **No compiler warning** | Capturing a reference to a local in a lambda is valid C++; neither `-Wall` nor `-Wextra` diagnoses it. Only thread-safety annotations or Clang's `-Wthread-safety` can flag it, and only with manual annotation. |

---

## 3. Expected Runtime Condition to Trigger

The crash fires when **all** of the following conditions are met simultaneously:

1. `AnalyticsConfig::enabled == true` (default).
2. `AnalyticsWorker::Start()` is called (happens at server startup when the analytics subsystem
   is initialized in the composition root).
3. The analytics worker survives for at least `flush_interval` milliseconds after `Start()` returns
   (default: the first 1 000 ms of server lifetime).
4. The OS thread scheduler has not yet terminated the detached thread before it wakes.

In a normal server run with the default configuration **all four conditions are met every time
the server starts with analytics enabled**, making the crash nearly deterministic in a
non-optimized build. In an optimized (`-O2`/`-O3`) production build compiler reuse of the stack
frame is more aggressive, so the crash window is wider and the symptom may manifest as silent
heap corruption several seconds later rather than an immediate segfault.

---

## 4. Steps to Reproduce (STR)

### Prerequisites

```bash
# Build with Debug symbols (recommended for clear stack traces)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF
cmake --build . -j$(nproc)
```

### Minimal reproduction

```bash
# Run with analytics enabled (default) — crash expected ~1 second after startup
./url_shortener --port 8080
```

Expected output before crash:

```
{"level":"info","component":"main","event":"startup_begin"}
{"level":"info","component":"main","event":"data_file_missing","path":"uri.txt"}
# ... server starts, accepts connections ...
# ~1 second later:
Segmentation fault (core dumped)
```

### Forced / accelerated reproduction

Set `flush_interval` to a very short value via config to shorten the crash window to
milliseconds:

```bash
# If a config file is supported, set analytics.flush_interval_ms = 10
./url_shortener --config /path/to/config.json
```

### With AddressSanitizer (deterministic)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build . -j$(nproc)
./url_shortener --port 8080
```

ASan output (immediate, deterministic):

```
==<PID>==ERROR: AddressSanitizer: stack-use-after-return on address 0x... at pc 0x...
READ of size 8 at 0x... thread T2
    #0 0x... in std::basic_string<char>::size() const
    #1 0x... in url_shortener::analytics::AnalyticsWorker::Start()::{lambda()#1}::operator()() const
       src/analytics/AnalyticsWorker.cpp:22
    #2 0x... in std::thread::_State_impl<...>::_M_run()
```

---

## 5. Revert

```bash
git revert <commit-hash>
# or
git checkout main -- src/analytics/AnalyticsWorker.cpp
```

The injected code is confined entirely to `AnalyticsWorker::Start()` in
`src/analytics/AnalyticsWorker.cpp`. No other file was modified.
