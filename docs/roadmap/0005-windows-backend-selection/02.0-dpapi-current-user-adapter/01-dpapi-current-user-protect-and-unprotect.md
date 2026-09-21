# 01 - DPAPI CurrentUser protect and unprotect

**Parent task:** 02.0 DPAPI CurrentUser adapter
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02

## Objective

Implement the core encrypt/decrypt operations for the `dpapi-user` backend
using `CryptProtectData`/`CryptUnprotectData` at CurrentUser scope.

## Files

- `include/url_shortener/secret_store/windows/dpapi_current_user_secret_store.hpp`
- `src/secret_store/windows/dpapi_current_user_secret_store.cpp`

## Behavior (plan.md Backend semantics: `dpapi-user`)

- Protect secret bytes with `CryptProtectData`, no `CRYPTPROTECT_LOCAL_MACHINE`
  flag - CurrentUser scope only.
- Decrypt with `CryptUnprotectData`; decryption only succeeds under the same
  Windows user profile that performed the protect call (this is inherent to
  DPAPI CurrentUser scope, not something the adapter has to enforce itself -
  the OS enforces it).
- Use this backend as the default for interactive use (the default-selection
  wiring itself is Task 05.0; this subtask just needs to be correct and safe
  to select by default).

## Shape

```cpp
class DpapiCurrentUserSecretStore final {
public:
    /** @brief Encrypt `plaintext` for the current Windows user. */
    std::vector<std::byte> Protect(std::span<const std::byte> plaintext) const;

    /**
     * @brief Decrypt `ciphertext` produced by Protect() under the same user
     *        profile.
     * @throws std::runtime_error on DPAPI failure, with the Windows error
     *         code but no ciphertext/plaintext content in the message
     *         (plan.md C3).
     */
    std::vector<std::byte> Unprotect(std::span<const std::byte> ciphertext) const;
};
```

## Constraints

- No LocalMachine flag anywhere in this file - that is Task 03.0's adapter,
  not this one.
- On `CryptProtectData`/`CryptUnprotectData` failure, surface
  `GetLastError()` in the thrown exception's message, never the input bytes
  (plan.md C3).
- RAII: any DPAPI-allocated output buffer (`LocalAlloc`'d `BLOB.pbData`) is
  released via a scope guard/RAII wrapper, not a manual free at every return
  path.

## Success criteria

- [ ] `Protect` then `Unprotect` round-trips arbitrary byte content
      correctly for the current user.
- [ ] `Unprotect` on ciphertext produced by a different Windows user account
      fails (verified in Task 06.0's integration test, not here - this
      subtask just needs to not special-case or work around that failure).
- [ ] A DPAPI failure produces an exception whose message contains the
      Windows error code and no secret bytes.
