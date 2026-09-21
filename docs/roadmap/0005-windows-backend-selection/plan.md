# 0005 Windows Backend Selection

## Background

`secret-store.exe` on Linux already lets callers choose an explicit
secret-storage backend instead of relying only on automatic detection. This
milestone brings the same explicit-selection capability to the Windows build
of `secret-store.exe`.

> **Unverified premise - confirm before implementation starts.** A repo-wide
> search (2026-09-21) found **no** trace of `secret-store.exe`, an
> `ISecretStore` interface, a Linux backend selector, or any DPAPI/Credential
> Manager/secret-store code anywhere in this checkout - not in `src/`,
> `include/`, `CMakeLists.txt`, or `sources.cmake`. Either this component
> lives outside this repository, uses different naming than this plan
> assumes, or this milestone is ahead of its prerequisite. Confirm the actual
> location/name of the Linux implementation (or that this milestone should
> introduce `ISecretStore` from scratch) before Task 01.0 begins. The File
> map below proposes a layout consistent with this repo's existing storage
> backend-selection pattern; it does not describe anything that currently
> exists.

The Windows executable must support exactly three explicit backend types:

| CLI value | Windows backend | Intended scope |
|---|---|---|
| `dpapi-user` | Windows DPAPI CurrentUser protection | Per-user local secrets; default for developer and desktop use |
| `dpapi-machine` | Windows DPAPI LocalMachine protection | Machine-wide service secrets for Windows services running under different accounts |
| `wincred` | Windows Credential Manager | User-visible credential entries managed through Windows credential APIs |

**Non-goals:**

- Do not change Linux backend names or Linux selection behavior.
- Do not add cross-platform dependencies for secret storage.
- Do not silently downgrade from a requested Windows backend to another
  backend.
- Do not log secret values, DPAPI plaintext, credential blobs, or access
  tokens.

## Architecture

Introduce a Windows backend-selection layer equivalent to the Linux selector:

1. Parse `--backend` into a platform-neutral enum or a Windows-specific enum.
2. Validate the requested value against the current platform.
3. Construct the matching Windows secret-store adapter.
4. Pass the adapter to existing command handlers.

Suggested C++ shape:

```cpp
enum class WindowsSecretBackend {
    dpapi_user,
    dpapi_machine,
    wincred
};

WindowsSecretBackend ParseWindowsSecretBackend(std::string_view value);
std::unique_ptr<ISecretStore> BuildWindowsSecretStore(WindowsSecretBackend backend);
```

Keep CLI parsing separate from backend construction so tests can cover
parsing, validation, and factory selection independently.

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| 01.0 | [CLI backend flag parsing & validation](/docs/roadmap/0005-windows-backend-selection/01.0-cli-backend-flag-parsing-and-validation/README.md) | Infrastructure | `WindowsSecretBackend` enum, `ParseWindowsSecretBackend`, `--backend` CLI wiring, and value/platform validation - no adapters yet. |
| 02.0 | [DPAPI CurrentUser adapter](/docs/roadmap/0005-windows-backend-selection/02.0-dpapi-current-user-adapter/README.md) | Adapter | `dpapi-user` backend implementation using `CryptProtectData`/`CryptUnprotectData` at CurrentUser scope. |
| 03.0 | [DPAPI LocalMachine adapter](/docs/roadmap/0005-windows-backend-selection/03.0-dpapi-local-machine-adapter/README.md) | Adapter | `dpapi-machine` backend implementation using `CryptProtectData`/`CryptUnprotectData` at LocalMachine scope. |
| 04.0 | [Windows Credential Manager adapter](/docs/roadmap/0005-windows-backend-selection/04.0-windows-credential-manager-adapter/README.md) | Adapter | `wincred` backend implementation over the Windows Credential Manager APIs. |
| 05.0 | [Factory wiring & auto-selection](/docs/roadmap/0005-windows-backend-selection/05.0-factory-wiring-and-auto-selection/README.md) | Infrastructure | `BuildWindowsSecretStore` factory, preserved auto-selection when `--backend` is omitted, non-secret diagnostic reporting. |
| 06.0 | [Test suite](/docs/roadmap/0005-windows-backend-selection/06.0-test-suite/README.md) | Testing | Fake-adapter unit tests for parsing, validation, and factory selection; opt-in Windows-only integration tests against real DPAPI/Credential Manager APIs. |
| 07.0 | [Documentation](/docs/roadmap/0005-windows-backend-selection/07.0-documentation/README.md) | Documentation | Operator/developer docs: backend table, DPAPI CurrentUser-vs-LocalMachine tradeoffs, Credential Manager naming rules, migration note. |

## Shared contracts (authoritative)

Cross-cutting rules every task must honor:

**C1. CLI contract & valid values.** `secret-store.exe` accepts an explicit
`--backend` option:

```powershell
secret-store.exe --backend dpapi-user <command> ...
secret-store.exe --backend dpapi-machine <command> ...
secret-store.exe --backend wincred <command> ...
```

The accepted values are Windows-specific (`dpapi-user`, `dpapi-machine`,
`wincred`). Invalid values must fail fast with a clear error listing the
valid Windows values. If `--backend` is omitted, Windows may keep its
existing auto-selection behavior, but the selected backend must be reported
in non-secret diagnostic output when the command already prints backend
information.

**C2. No silent fallback.** A requested backend must never silently downgrade
to a different backend. An unsupported backend value is a fatal error (exit
non-zero); it is never treated as "fall back to auto-selection."

**C3. No secret material in logs/diagnostics.** Never log secret values,
DPAPI plaintext, credential blobs, or access tokens. When a backend API call
fails, surface the Windows error code in diagnostics without printing secret
material.

**C4. Linux backend names are always rejected on Windows.** Platform-specific
validation must reject Linux backend names when running on Windows, exiting
non-zero and explaining the valid Windows values.

**C5. Windows backends are Windows-only.** Requesting a Windows backend value
on a non-Windows platform/build exits non-zero and explains that the backend
is Windows-only. Non-Windows builds either compile out the Windows backends
entirely or reject them deterministically at runtime.

**C6. No cross-platform secret-storage dependency.** Windows backend
implementations must not introduce a dependency shared with (or required by)
the Linux backend selection path; Linux backend names and Linux selection
behavior are unchanged by this milestone.

## Dependency graph

```text
01.0 cli-backend-flag-parsing-and-validation
   |  (WindowsSecretBackend enum, ParseWindowsSecretBackend, --backend wiring,
   |   value + platform validation; no adapters yet)
   |
   +--> 02.0 dpapi-current-user-adapter   -\
   +--> 03.0 dpapi-local-machine-adapter    |  (independent adapters; each only
   +--> 04.0 windows-credential-manager-adapter -/  depends on 01.0's enum)
   |
   v
05.0 factory-wiring-and-auto-selection
   |  (BuildWindowsSecretStore ties 02.0/03.0/04.0 adapters to the parsed
   |   backend enum; wires omitted-flag auto-selection + diagnostics)
   v
06.0 test-suite
   |  (fake-adapter unit tests over 01.0-05.0; opt-in integration tests
   |   against real DPAPI/Credential Manager APIs)
   v
07.0 documentation
      (operator/developer docs sync once behavior is implemented and tested)
```

## File map

Note: `ISecretStore` (`i_secret_store.hpp`) could **not** be confirmed to
exist in this repository (see the verification note above) - it is shown
here as the interface this milestone should conform to *if* it already
exists elsewhere, or introduce *if* it doesn't. Naming/layout below follows
this repository's existing, verified storage backend-selection pattern
(`include/url_shortener/config/storage_config.hpp` +
`include/url_shortener/composition/storage_factory.hpp` +
`include/url_shortener/storage/i_*.hpp`, all of which do exist today),
adapted to a new `secret_store` module.

```text
include/url_shortener/secret_store/
  i_secret_store.hpp                     (existing; cross-platform interface, not modified here)
  windows/
    windows_secret_backend.hpp           (WindowsSecretBackend enum + ParseWindowsSecretBackend)
    windows_secret_store_factory.hpp     (BuildWindowsSecretStore)
    dpapi_current_user_secret_store.hpp
    dpapi_local_machine_secret_store.hpp
    wincred_secret_store.hpp

src/secret_store/
  main_secret_store.cpp                  (secret-store.exe entry point; --backend wiring)
  windows/
    windows_secret_backend.cpp
    windows_secret_store_factory.cpp
    dpapi_current_user_secret_store.cpp
    dpapi_local_machine_secret_store.cpp
    wincred_secret_store.cpp

tests/unit/secret_store/windows/
  01_parse_valid_backend_values.cpp
  02_invalid_backend_value_rejected.cpp
  03_linux_backend_name_rejected_on_windows.cpp
  04_windows_backend_rejected_on_non_windows_platform.cpp
  05_factory_selects_dpapi_user_adapter.cpp
  06_factory_selects_dpapi_machine_adapter.cpp
  07_factory_selects_wincred_adapter.cpp
  08_omitted_backend_preserves_auto_selection.cpp
  09_no_secret_material_in_diagnostic_output.cpp

tests/integration/secret_store/windows/
  01_dpapi_current_user_roundtrip.cpp    (opt-in, Windows-only, real DPAPI CurrentUser)
  02_dpapi_local_machine_roundtrip.cpp   (opt-in, Windows-only, real DPAPI LocalMachine)
  03_wincred_roundtrip.cpp               (opt-in, Windows-only, real Credential Manager)

docs/security/
  windows-secret-backends.md             (backend table, DPAPI tradeoffs, wincred naming rules, migration note)
```

New `.cpp` files must be registered in `sources.cmake` and, when a new test
binary is added, in `CMakeLists.txt` (same convention as milestone 0001, see
[docs/roadmap/0001-rest-api-refactoring/plan.md](/docs/roadmap/0001-rest-api-refactoring/plan.md)
shared contract C8).

## Global acceptance criteria

- [ ] `secret-store.exe --backend dpapi-user ...` uses DPAPI CurrentUser.
- [ ] `secret-store.exe --backend dpapi-machine ...` uses DPAPI LocalMachine.
- [ ] `secret-store.exe --backend wincred ...` uses Windows Credential
      Manager.
- [ ] Invalid or platform-inappropriate backend values fail clearly (C1, C4,
      C5).
- [ ] Existing Linux behavior remains unchanged (C6).
- [ ] Tests pass on Windows, and non-Windows builds either compile out
      Windows backends or reject them deterministically (C5).
