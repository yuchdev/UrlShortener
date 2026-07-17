# Windows secret-store backend selection plan

## Goal

Make `secret-store.exe` on Windows accept an explicit secret-storage backend type,
matching the Linux CLI behavior where callers can choose the backend instead of
depending only on automatic detection.

The Windows executable must support exactly three explicit backend types:

| CLI value | Windows backend | Intended scope |
|---|---|---|
| `dpapi-user` | Windows DPAPI CurrentUser protection | Per-user local secrets; default for developer and desktop use |
| `dpapi-machine` | Windows DPAPI LocalMachine protection | Machine-wide service secrets for Windows services running under different accounts |
| `wincred` | Windows Credential Manager | User-visible credential entries managed through Windows credential APIs |

## Non-goals

- Do not change Linux backend names or Linux selection behavior.
- Do not add cross-platform dependencies for secret storage.
- Do not silently downgrade from a requested Windows backend to another backend.
- Do not log secret values, DPAPI plaintext, credential blobs, or access tokens.

## CLI contract

`secret-store.exe` must accept an explicit backend option:

```powershell
secret-store.exe --backend dpapi-user <command> ...
secret-store.exe --backend dpapi-machine <command> ...
secret-store.exe --backend wincred <command> ...
```

The accepted values are Windows-specific. Invalid values must fail fast with a
clear error listing the valid Windows values.

If `--backend` is omitted, Windows may keep its existing auto-selection behavior,
but the selected backend must be reported in non-secret diagnostic output when
the command already prints backend information.

## Backend semantics

### `dpapi-user`

- Protect with `CryptProtectData` using current-user scope.
- Decrypt only under the same Windows user profile.
- Use as the default backend for interactive use.
- Tests should verify that backend selection reaches the CurrentUser adapter and
  does not call machine-scope or Credential Manager code.

### `dpapi-machine`

- Protect with `CryptProtectData` using LocalMachine scope.
- Use for services that need secrets readable by more than one service account on
  the same host.
- Document that LocalMachine scope is broader than CurrentUser and should be
  paired with filesystem ACLs on any persisted ciphertext.
- Tests should verify the LocalMachine adapter is selected only when requested.

### `wincred`

- Store entries through the Windows Credential Manager APIs.
- Use stable target names that include application namespace and secret key.
- Preserve existing create/read/update/delete command behavior.
- Tests should use a fake Credential Manager adapter; do not write real user
  credentials during unit tests.

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

Keep CLI parsing separate from backend construction so tests can cover parsing,
validation, and factory selection independently.

## Error handling

- Unsupported backend value: exit non-zero and print valid values.
- Windows backend requested on non-Windows: exit non-zero and explain the backend
  is Windows-only.
- Linux backend requested on Windows: exit non-zero and explain the valid Windows
  values.
- Backend API failure: surface the Windows error code without printing secret
  material.

## Tests

Add Windows-focused tests for:

1. `--backend dpapi-user` selects the DPAPI CurrentUser adapter.
2. `--backend dpapi-machine` selects the DPAPI LocalMachine adapter.
3. `--backend wincred` selects the Credential Manager adapter.
4. Invalid backend values fail with the valid Windows values in the message.
5. Omitted backend preserves current auto-selection behavior.
6. Linux-only backend names are rejected on Windows.
7. No test output contains secret values.

Unit tests should use fake adapters. Integration tests that touch real DPAPI or
Credential Manager APIs must be opt-in and clearly marked as Windows-only.

## Documentation updates

Update the operator/developer documentation with:

- Windows backend table and examples.
- Security tradeoffs between CurrentUser and LocalMachine DPAPI.
- Credential Manager naming rules.
- Migration note for users currently relying on automatic Windows backend
  selection.

## Acceptance criteria

- `secret-store.exe --backend dpapi-user ...` uses DPAPI CurrentUser.
- `secret-store.exe --backend dpapi-machine ...` uses DPAPI LocalMachine.
- `secret-store.exe --backend wincred ...` uses Windows Credential Manager.
- Invalid or platform-inappropriate backend values fail clearly.
- Existing Linux behavior remains unchanged.
- Tests pass on Windows, and non-Windows builds either compile out Windows
  backends or reject them deterministically.
