# Crash Dump Guide — url_shortener

This document covers how to collect, configure, and interpret crash dumps for the
`url_shortener` binary. It explains why different crash mechanisms require different
collection strategies, and documents the exact setup used in this repository.

---

## 1. Dump types

Windows produces several distinct dump formats. Choosing the wrong one means either
missing the data you need or waiting minutes for a 4 GB file.

| Type | Flag (WER / ProcDump) | Contains | Typical size |
|---|---|---|---|
| **Minidump** | `DumpType=1` / `-mm` | Thread stacks, loaded modules, exception record | 1–5 MB |
| **Full minidump** | `DumpType=2` / `-ma` | All of the above + full process heap and memory | 30–500 MB |
| **Custom (triage)** | `DumpType=0` / `-mt` | Minimal: exception record + calling thread only | < 1 MB |
| **Heap dump** | — / `-mh` | Full minidump + additional heap metadata | > full dump |

**Rule of thumb for this project:**  
Use `-ma` (full minidump). The analytics queue, Boost.Beast connection objects, and SOCI
connection pools all live on the heap; a minidump without heap content gives you only stack
frames and no variable values.

---

## 2. Why `STATUS_FAST_FAIL` (0xC0000409) bypasses Windows Error Reporting

The crash injected in `AnalyticsWorker::Start()` (an unjoined `std::thread` destructor
calling `std::terminate()`) terminates the process via `__fastfail()`, not via a normal
exception. This matters because:

- **Normal exceptions** (access violations, stack overflows, heap corruption) flow through
  the Structured Exception Handling (SEH) chain → kernel's last-chance handler → WER.
- **`__fastfail()`** calls directly into the kernel via `int 0x29` / `ud2` depending on
  architecture. It bypasses every user-mode handler including WER's `WerFault.exe`.

Consequence: even with `HKLM\SOFTWARE\Microsoft\Windows\Windows Error
Reporting\LocalDumps` correctly configured, a `STATUS_FAST_FAIL` crash writes a 0-byte
file and never notifies WER. ProcDump intercepts the exception at the kernel level before
`__fastfail` completes, which is why it succeeds where WER fails.

### Crash mechanisms and which collector captures them

| Crash mechanism | SEH accessible | WER LocalDumps | ProcDump `-e 1` |
|---|---|---|---|
| Access violation (`SIGSEGV` / `0xC0000005`) | ✅ | ✅ | ✅ |
| Stack overflow (`0xC00000FD`) | ✅ (on alt stack) | ✅ | ✅ |
| `abort()` / `raise(SIGABRT)` | ✅ | ✅ | ✅ |
| `std::terminate()` (unjoined thread) | ✅ internally | ✅ | ✅ |
| **`__fastfail()` / `STATUS_FAST_FAIL` (0xC0000409)** | ❌ | ❌ (0-byte dump) | ✅ |
| `TerminateProcess()` | ❌ | ❌ | ❌ (process gone) |

---

## 3. WER LocalDumps configuration (Windows)

WER LocalDumps is the simplest always-on option for crashes that go through SEH.  
It requires administrator rights to configure.

```powershell
# Run as Administrator
$key = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\url_shortener.exe"
New-Item -Path $key -Force
Set-ItemProperty $key -Name "DumpFolder" -Value "C:\Dumps\url_shortener"
Set-ItemProperty $key -Name "DumpType"   -Value 2    # 1=mini, 2=full
Set-ItemProperty $key -Name "DumpCount"  -Value 5    # keep last 5

New-Item -Path "C:\Dumps\url_shortener" -Force -ItemType Directory
```

**Verify the configuration:**
```powershell
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\url_shortener.exe"
```

**Limitations:**
- Does not capture `STATUS_FAST_FAIL` (see §2).
- The `DumpFolder` must exist before the crash; WER does not create it.
- Requires a reboot or process restart to take effect for already-running processes.

---

## 4. ProcDump — the reliable fallback

ProcDump (Sysinternals) is installed at `C:\Tools\procdump64.exe` in this environment.
It hooks into the process at launch and can capture dumps for exception codes that
WER cannot intercept, including `0xC0000409`.

### 4.1 One-shot crash capture (launch and monitor)

```powershell
C:\Tools\procdump64.exe -accepteula -e 1 -ma `
    -x "$env:LOCALAPPDATA\CrashDumps" `
    ".\cmake-build\RelWithDebInfo\url_shortener.exe" `
    "--http-port" "8080"
```

| Flag | Meaning |
|---|---|
| `-e 1` | Capture on first unhandled exception (not first-chance) |
| `-ma` | Full minidump (heap + all memory) |
| `-x <dir>` | Launch the executable; write dumps to `<dir>` |
| Trailing args | Passed verbatim to `url_shortener.exe` — must be separate tokens, not a quoted string |

> **Important:** Pass exe arguments as separate tokens.  
> ✅ `"url_shortener.exe" "--http-port" "8080"`  
> ❌ `"url_shortener.exe" "--http-port 8080"` ← parsed as one arg → Boost.ProgramOptions exception

### 4.2 Attach to a running process

```powershell
# By PID
C:\Tools\procdump64.exe -accepteula -e 1 -ma -x "$env:LOCALAPPDATA\CrashDumps" <PID>

# By process name
C:\Tools\procdump64.exe -accepteula -e 1 -ma -x "$env:LOCALAPPDATA\CrashDumps" url_shortener.exe
```

### 4.3 Capture specific exception codes only

```powershell
# Only STATUS_FAST_FAIL
C:\Tools\procdump64.exe -accepteula -e -f "C0000409" -ma `
    -x "$env:LOCALAPPDATA\CrashDumps" `
    ".\cmake-build\RelWithDebInfo\url_shortener.exe" "--http-port" "8080"
```

### 4.4 Download ProcDump (if not present)

```powershell
Invoke-WebRequest -Uri "https://download.sysinternals.com/files/Procdump.zip" `
    -OutFile "$env:TEMP\Procdump.zip" -UseBasicParsing
Expand-Archive -Path "$env:TEMP\Procdump.zip" -DestinationPath "C:\Tools" -Force
```

---

## 5. Linux — core dump setup

On Linux (Ubuntu 24.04, the CI baseline), crashes produce core files instead of `.dmp`.

### 5.1 Enable core dumps

```bash
# Current session only
ulimit -c unlimited

# Persist across sessions (add to /etc/security/limits.conf)
echo "* soft core unlimited" | sudo tee -a /etc/security/limits.conf
echo "* hard core unlimited" | sudo tee -a /etc/security/limits.conf
```

### 5.2 Configure core file location and naming

```bash
# Write cores to /var/crash with PID and exe name
sudo bash -c 'echo "/var/crash/core.%e.%p" > /proc/sys/kernel/core_pattern'
sudo mkdir -p /var/crash && sudo chmod 1777 /var/crash
```

### 5.3 Run and capture

```bash
ulimit -c unlimited
./build/url_shortener --http-port 8080
# → core.url_shortener.<PID> in /var/crash

# Inspect with GDB
gdb ./build/url_shortener /var/crash/core.url_shortener.<PID>
(gdb) bt full
(gdb) info threads
```

### 5.4 Using AddressSanitizer instead (preferred for UAF bugs)

ASan produces a detailed report with stack traces for both the crash site and the
allocation/deallocation sites — far more informative than a raw core for use-after-free.

```bash
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan --target url_shortener
./build-asan/url_shortener --http-port 8080
```

---

## 6. Comparing dumps from the injected fault

The fault in `AnalyticsWorker::Start()` is an unjoined `std::thread queue_probe` whose
destructor calls `std::terminate()` → `__fastfail(FAST_FAIL_FATAL_APP_EXIT)`.

### Expected ProcDump output

```
[10:46:11] Unhandled: C0000409
[10:46:11] Dump 1 initiated: url_shortener.exe_YYMMDD_HHMMSS.dmp
[10:46:11] Dump 1 complete: 32 MB written in 0.1 seconds
```

### Opening the dump in WinDbg

```
windbg -z url_shortener.exe_260615_104611.dmp
```

In WinDbg:
```
!analyze -v           # automated crash analysis
~*kv                  # stacks of all threads
.ecxr                 # switch to exception context
kv 20                 # show 20 frames of current thread
```

Expected call stack leading to the crash:
```
url_shortener!url_shortener::analytics::AnalyticsWorker::Start
  → std::thread::~thread          ← queue_probe destructor
    → std::terminate
      → abort / __fastfail
```

### Opening the dump in Visual Studio

1. **File → Open → File** → select the `.dmp`
2. Click **"Debug with Native Only"**
3. Visual Studio shows the thread stacks at crash time, local variables, and the exact
   source line if PDBs are present (`cmake-build\RelWithDebInfo\url_shortener.pdb`).

---

## 7. Quick reference

```powershell
# Install ProcDump
Invoke-WebRequest https://download.sysinternals.com/files/Procdump.zip `
    -OutFile "$env:TEMP\pd.zip" -UseBasicParsing
Expand-Archive "$env:TEMP\pd.zip" C:\Tools -Force

# Capture crash dump (works for STATUS_FAST_FAIL)
C:\Tools\procdump64.exe -accepteula -e 1 -ma `
    -x "$env:LOCALAPPDATA\CrashDumps" `
    ".\cmake-build\RelWithDebInfo\url_shortener.exe" "--http-port" "8080"

# Check for collected dumps
Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -Filter "*.dmp" |
    Select-Object Name, LastWriteTime, @{N="MB";E={[math]::Round($_.Length/1MB,1)}}
```

```bash
# Linux: capture core
ulimit -c unlimited && ./build/url_shortener --http-port 8080

# Linux: analyze with GDB
gdb ./build/url_shortener core.<PID>

# Linux: sanitizer build (preferred for thread/memory bugs)
cmake -S . -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,thread"
cmake --build build-asan --target url_shortener
./build-asan/url_shortener --http-port 8080
```
