# 01 - wincred target naming and CRUD preservation

**Parent task:** 04.0 Windows Credential Manager adapter
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02

## Objective

Implement the Credential Manager CRUD operations (create/read/update/delete)
behind stable, namespaced target names.

## Files

- `include/url_shortener/secret_store/windows/wincred_secret_store.hpp`
- `src/secret_store/windows/wincred_secret_store.cpp`

## Behavior (plan.md Backend semantics: `wincred`)

- Store entries through the Windows Credential Manager APIs (`CredWriteW`,
  `CredReadW`, `CredDeleteW`).
- Use stable target names of the form `<application-namespace>:<secret-key>`
  (exact namespace string to be fixed to the project's existing CLI/app
  identifier - reuse whatever identifier the Linux backend or the CLI itself
  already uses for namespacing, do not invent a second one).
- Preserve existing create/read/update/delete command behavior - a
  `wincred`-backed create must behave the same as any other backend's create
  from the caller's perspective (same success/failure semantics, same
  idempotency rules for update).

## Shape

```cpp
class WincredSecretStore final {
public:
    void Create(std::string_view key, std::span<const std::byte> value) const;
    std::vector<std::byte> Read(std::string_view key) const;
    void Update(std::string_view key, std::span<const std::byte> value) const;
    void Delete(std::string_view key) const;

private:
    static std::wstring TargetNameFor(std::string_view key);
};
```

`TargetNameFor` is the single place that builds
`<application-namespace>:<secret-key>` - every Credential Manager call must
go through it so target-name construction cannot drift between methods.

## Constraints

- Windows Credential Manager credential blobs have a documented maximum size
  (`CRED_MAX_CREDENTIAL_BLOB_SIZE`) - `Create`/`Update` must reject
  oversized values with a clear error rather than truncating silently.
- On Credential Manager API failure, surface `GetLastError()` without the
  credential value in the error message (plan.md C3).
- Structure the class so the underlying `Cred*` API calls can be substituted
  with a fake in tests (Task 06.0) - e.g. take the Win32 credential API
  surface as an injectable dependency rather than calling the global
  functions directly, matching the RAII/testability conventions already used
  elsewhere in this codebase.

## Success criteria

- [ ] `Create` then `Read` round-trips a value under a target name of the
      form `<application-namespace>:<secret-key>`.
- [ ] `Update` overwrites the existing entry for the same key rather than
      creating a duplicate target.
- [ ] `Delete` removes the entry; a subsequent `Read` fails cleanly.
- [ ] Oversized values are rejected with a clear error before any
      `CredWrite` call is attempted.
- [ ] A Credential Manager API failure produces an exception with the
      Windows error code and no credential value.
