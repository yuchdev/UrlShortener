# Fault Injection Log — Aegis SWR Forensic Analyzer

**Branch:** `feature/fault-injection-aegis`

**Target file:** `src/analytics/AnalyticsWorker.cpp`

---

## ⚠ Forensic Addendum — Why Original Bug Did Not Crash

**Date:** 2026-06-15  
**Finding author:** Investigation of `feature/fault-injection-aegis`

### Root cause 1 (primary): `AnalyticsWorker` not wired into the server binary

`AnalyticsWorker::Start()` is never called during server startup.  
`main.cpp` instantiates only `HttpServer`; it has no reference to `AnalyticsWorker`.  
No code in `src/` outside of `src/analytics/AnalyticsWorker.cpp` itself instantiates or
starts a worker. The fault injection can only manifest inside unit tests, not in the
live server process.

### Root cause 2 (secondary): probe capture was silently patched to by-value

The injection log below specifies `[this, &probe_label]` (reference capture — UAF).  
The actual committed code uses `[probe_delay, probe_label, &metrics]` (value capture
for `probe_label` — no UAF). Whether this was an oversight during injection or a
subsequent hotfix, the lambda no longer holds a dangling reference.

### Resolution

The `probe_label` fix (value capture) is retained. A **replacement subtle bug** of a
different thread-safety archetype has been introduced in `Start()` to satisfy the
forensic exercise. See **Section 7** below.

---

**Original Bug archetype:** Asynchronous Lambda Reference Capture (Use-After-Free)

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

`probe_label` is a `std::string` local variable with **automatic (stack) storage duration** in `AnalyticsWorker::Start()`. Its lifetime ends when `Start()` returns.

The lambda **captures `probe_label` by reference** (`[this, &probe_label]`). The `std::thread` is immediately `.detach()`-ed, making it fully independent of `Start()`'s call frame.

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
| --- | --- |
| **Delayed trigger** | Crash occurs ≈ `flush_interval` (default 1 s) after `Start()` is called, not at startup. |
| **Race-dependent** | If the stack frame of `Start()` is not reused before the thread wakes up, the corrupted read may silently return a stale (but non-trapping) value, deferring the crash further or masking it entirely. |
| **Salt-dependent** | `probe_label` overflows SSO only when `client_hash_salt` is non-empty. With an empty salt the string `"analytics:probe::init"` is 21 characters — still above the libstdc++ SSO threshold of 15, so the heap path is always taken in practice. |
| **Platform variance** | On systems with aggressive heap poisoning (ASan, glibc `MALLOC_PERTURB_`) the crash is immediate and deterministic; on production builds without those guards it is intermittent. |
| **No compiler warning** | Capturing a reference to a local in a lambda is valid C++; neither `-Wall` nor `-Wextra` diagnoses it. Only thread-safety annotations or Clang's `-Wthread-safety` can flag it, and only with manual annotation. |

---

## 3. Expected Runtime Condition to Trigger

The crash fires when **all** of the following conditions are met simultaneously:

1. `AnalyticsConfig::enabled == true` (default).
2. `AnalyticsWorker::Start()` is called (happens at server startup when the analytics subsystem is initialized in the composition root).
3. The analytics worker survives for at least `flush_interval` milliseconds after `Start()` returns (default: the first 1 000 ms of server lifetime).
4. The OS thread scheduler has not yet terminated the detached thread before it wakes.

In a normal server run with the default configuration **all four conditions are met every time the server starts with analytics enabled**, making the crash nearly deterministic in a non-optimized build. In an optimized (`-O2`/`-O3`) production build compiler reuse of the stack frame is more aggressive, so the crash window is wider and the symptom may manifest as silent heap corruption several seconds later rather than an immediate segfault.

---

## 4. Steps to Reproduce (STR)

### Prerequisites & Environment Setup

To closely simulate a production environment where aggressive compiler optimizations accelerate stack frame reuse, compile the binary using Release with Debug Information (`RelWithDebInfo`). This creates the optimized execution paths while generating full debug symbols (`.pdb` on Windows or embedded symbols on Ubuntu) for forensic logging.

#### Ubuntu (Linux) Setup

```bash
# Build with Release optimizations and embedded debug symbols
mkdir -p cmake-build && cd cmake-build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
cmake --build . -j$(nproc)

# Enable core dumps for the current shell session
ulimit -c unlimited

```

#### Windows (MSVC) Setup

```cmd
:: Generate build files and compile the Release configuration with PDB symbols
mkdir cmake-build && cd cmake-build
cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config RelWithDebInfo --target url_shortener

:: Configure Windows Error Reporting (WER) to save local crash dumps (Run PowerShell as Admin)
powershell -Command "$path = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\url_shortener.exe'; if (-not (Test-Path $path)) { New-Item -Path $path -Force }; New-ItemProperty -Path $path -Name 'DumpFolder' -Value 'C:\CrashDumps' -PropertyType ExpandString -Force; New-ItemProperty -Path $path -Name 'DumpType' -Value 2 -PropertyType DWord -Force; New-Item -ItemType Directory -Force -Path 'C:\CrashDumps'"

```

---

### Minimal Reproduction & Forensic Collection

#### Execution on Ubuntu

```bash
# Run with analytics enabled (default) and pipe all streams to a log file
cd RelWithDebInfo
./url_shortener --http-port 8080

```

Expected log output inside `server_ubuntu.log` before the crash:

```
{"level":"info","component":"main","event":"startup_begin"}
{"level":"info","component":"main","event":"data_file_missing","path":"uri.txt"}

```

*The application will abort within ~1 second due to a `SIGSEGV`.*

Extract the core dump file if managed by `systemd-coredump`:

```bash
coredumpctl dump url_shortener -o url_shortener_ubuntu.core

```

#### Execution on Windows

```cmd
:: Run the optimized executable and capture standard outputs
url_shortener.exe --http-port 8080 > server_windows.log 2>&1

```

*The application will crash silently with an Access Violation code (`0xC0000005`).*

Collect the forensic crash dump and symbols from their respective paths:

* **Log file:** `server_windows.log`
* **Symbols:** `cmake-build\RelWithDebInfo\url_shortener.pdb`
* **Crash Dump:** `C:\CrashDumps\url_shortener.exe.<PID>.dmp`

---

### Forced / accelerated reproduction

Set `flush_interval` to a very short value via config to shorten the crash window to milliseconds:

```bash
# If a config file is supported, set analytics.flush_interval_ms = 10
./url_shortener --config /path/to/config.json

```

---

### With AddressSanitizer (deterministic)

```bash
# Build with optimized RelWithDebInfo flags combined with AddressSanitizer
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build . -j$(nproc)
./url_shortener --http-port 8080

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

The injected code is confined entirely to `AnalyticsWorker::Start()` in `src/analytics/AnalyticsWorker.cpp`. This change set also modifies `FAULT_INJECTION_LOG.md` and adds `tests/unit/analytics/16_analytics_worker_start_probe_uaf.cpp`.

---

## 6. Exact Reproduction Commands

### 6.1 Build the dedicated UAF detection test (Debug, no sanitizer)

```bash
mkdir -p build_debug && cd build_debug
cmake .. -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target analytics__16_analytics_worker_start_probe_uaf -j$(nproc)
```

Run:

```bash
./analytics__16_analytics_worker_start_probe_uaf
```

Expected failure output (stack poisoning exposes corrupted `_M_string_length`):

```
Running 1 test case...
tests/unit/analytics/16_analytics_worker_start_probe_uaf.cpp(156): error:
  in "start_probe_must_not_hold_dangling_stack_reference":
  check m.last_depth.load() == expected_depth has failed [140429788944935 != 24]

*** 1 failure is detected in the test module "AnalyticsWorkerStartProbeUAF"
```

> The exact garbage value (e.g., `140429788944935`) varies by run; any value ≠ 24 confirms the UAF. If the process instead crashes with `SIGSEGV` during the test, that is also a confirmed UAF.

---

### 6.2 Build and run with AddressSanitizer (deterministic)

```bash
mkdir -p build_asan && cd build_asan
cmake .. -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build . --target analytics__16_analytics_worker_start_probe_uaf -j$(nproc)
```

Run:

```bash
ASAN_OPTIONS=detect_stack_use_after_return=1 \
  ./analytics__16_analytics_worker_start_probe_uaf
```

Expected ASan output (process crashes before assertions):

```
=================================================================
==<PID>==ERROR: AddressSanitizer: stack-use-after-return on address 0x7f... at pc 0x...
READ of size 8 at 0x7f... thread T2
    #0 0x... in std::__cxx11::basic_string<char>::size() const
    #1 0x... in url_shortener::analytics::AnalyticsWorker::Start()::{lambda()#1}::operator()() const
           src/analytics/AnalyticsWorker.cpp:22
    #2 0x... in std::thread::_State_impl<std::thread::_Invoker<std::tuple<...>>>::_M_run()

Address 0x7f... is located in stack of thread T1 at offset N in frame
  #0 0x... in url_shortener::analytics::AnalyticsWorker::Start()
      src/analytics/AnalyticsWorker.cpp:8

  This frame has 1 object(s):
    [32, 64) 'probe_label' (line 16) <== Memory access at offset N overlaps this variable
HINT: this may be a stack-use-after-return bug. Enable detection by setting
      ASAN_OPTIONS=detect_stack_use_after_return=1
```

---

### 6.3 Run the full analytics unit suite

```bash
cd build_debug   # or build_asan
ctest -R "analytics__" -V 2>&1 | grep -E "PASS|FAIL|error"
```

With the bug present, `analytics__16_analytics_worker_start_probe_uaf` will FAIL. All other analytics tests are expected to PASS.

---

### 6.4 Reproduce the runtime crash in the full server binary

```bash
# Build server (no test infra needed)
mkdir -p build_srv && cd build_srv
cmake .. -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target url_shortener -j$(nproc)

# Run — crash occurs ~1 s after startup (after default flush_interval elapses)
./url_shortener --http-port 8080
```

Expected output before crash:

```
{"level":"info","component":"main","event":"startup_begin"}
{"level":"info","component":"main","event":"data_file_missing","path":"uri.txt"}
Segmentation fault (core dumped)
```

Generate a core dump for forensic analysis:

```bash
ulimit -c unlimited
./url_shortener --http-port 8080
# After crash:
gdb ./url_shortener core -ex "thread apply all bt" -ex quit
```

---

## 7. Replacement Bug — Unjoined Local Thread (`std::terminate` on `Start()` return)

**Date injected:** 2026-06-15  
**Archetype:** Joinable Thread Destructor (`std::terminate`)  
**Severity:** Fatal — `std::terminate()` → `std::abort()` / `STATUS_FAST_FAIL (0xC0000409)`

### 7.1 Injected code (in `AnalyticsWorker::Start()`)

```cpp
// One-shot startup validation: verify the queue is in a consistent state
// before the persistent worker takes over.
std::thread queue_probe([this]() {
    metrics_.SetQueueDepth(queue_.GetStats().depth);
});

thread_ = std::thread([this] { Run(); });
// ← Start() returns here.  queue_probe goes out of scope.
// std::thread::~thread() is called on a joinable thread → std::terminate().
```

### 7.2 Root-cause analysis

In C++, destroying a `std::thread` object while it is **joinable** calls `std::terminate()`.
A thread remains joinable from the moment it is constructed until `join()` or `detach()` is
explicitly called — **even if its thread function has already returned**.

`queue_probe` is a local variable in `Start()`. Its thread function is a trivial one-liner
that likely completes in microseconds. But `queue_probe` is **never joined or detached**.
When `Start()` returns, `queue_probe`'s destructor runs. Since `queue_probe.joinable()` is
`true`, `std::terminate()` is invoked, which calls `std::abort()`.

The crash is **100% deterministic** — it fires every time `Start()` returns, regardless of
whether the thread function has actually finished executing.

**Relationship to server wiring (`main.cpp`):**  
`AnalyticsWorker::Start()` is now called from the server startup path in `main.cpp`,
immediately after `HttpServer::run()` posts the HTTP listener. The startup sequence
logs `http_listener_started`, then crashes before the `io_context.run()` event loop starts.

### 7.3 Execution timeline

```
T+0ms   server.run() → HTTP listener starts → logs "http_listener_started"
T+0ms   analytics_worker.Start() called
T+0ms     state_ = Running
T+0ms     detached probe thread spawned (sleeps flush_interval, irrelevant)
T+0ms     queue_probe thread created (joinable) — thread function may complete immediately
T+0ms     thread_ = std::thread([this] { Run(); })
T+0ms   Start() body ends, function prepares to return
T+0ms   ~std::thread() called on local queue_probe
           queue_probe.joinable() == true (even if thread function already finished)
           → std::terminate() → std::abort()
T+0ms   Process terminates (STATUS_FAST_FAIL / SIGABRT)
```

### 7.4 Why it is elusive

| Property | Detail |
|---|---|
| **Looks like defensive code** | `queue_probe` is introduced as a "startup validation" step; the thread function is a harmless queue depth read. Reviewers see intent, not a bug. |
| **No join/detach is easy to miss** | Every other `std::thread` in `Start()` is properly handled (`.detach()` on the probe, `thread_ =` for the worker). The omission of `queue_probe.join()` looks like an oversight in otherwise careful code. |
| **Thread may finish before destructor** | The `queue_probe` function runs in microseconds. A reader might reason: "it'll finish before `Start()` returns, so the destructor will see a finished thread — that's fine." But `joinable()` stays `true` regardless. |
| **No compiler warning** | `-Wall`, `-Wextra`, `-Wthread-safety` produce no diagnostic for an unjoined `std::thread`. |
| **Crash is immediate and reproducible** | Unlike the original probe UAF (which needed 1 second for the thread to sleep), this crash fires instantly and consistently every time. |

### 7.5 Expected output before crash

```
ts=...Z level=INFO component=main msg=startup_begin
ts=...Z level=INFO component=main msg=data_loaded path=uri.txt
ts=...Z level=INFO component=http_server msg=http_listener_started
[std::terminate → process exit -1073740791 (0xC0000409 STATUS_FAST_FAIL) on Windows]
[std::terminate → SIGABRT on Linux]
```

### 7.6 Reproduce

```powershell
# Windows (RelWithDebInfo)
.\cmake-build\RelWithDebInfo\url_shortener.exe --http-port 8080
# → immediate crash after http_listener_started log line
```

```bash
# Linux (RelWithDebInfo)
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target url_shortener
./build/url_shortener --http-port 8080
# → SIGABRT immediately after http_listener_started
```

### 7.7 Revert

Two files must be reverted together:

**`src/analytics/AnalyticsWorker.cpp`** — remove `queue_probe`, restore:
```cpp
thread_ = std::thread([this] { Run(); });
```

**`src/main.cpp`** — remove the analytics worker startup block (after `server.run()`),
remove the two analytics includes at the top of the file.

