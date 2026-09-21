# 01 - Windows secret backend enum and parser

**Parent task:** 01.0 CLI backend flag parsing & validation
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02, 03

## Objective

Define the `WindowsSecretBackend` enum and `ParseWindowsSecretBackend`
function that every later task (adapters, factory, tests) is built against.

## Files

- `include/url_shortener/secret_store/windows/windows_secret_backend.hpp`
- `src/secret_store/windows/windows_secret_backend.cpp`

## Shape

```cpp
enum class WindowsSecretBackend {
    dpapi_user,
    dpapi_machine,
    wincred
};

/**
 * @brief Parse a `--backend` value string into a WindowsSecretBackend.
 *
 * @param value Raw CLI value, e.g. "dpapi-user", "dpapi-machine", "wincred".
 * @throws std::runtime_error if value is not one of the three accepted
 *         Windows values, or is a recognized non-Windows (Linux) backend
 *         name, or is empty/unrecognized. The exception message lists the
 *         valid Windows values (see plan.md C1).
 */
WindowsSecretBackend ParseWindowsSecretBackend(std::string_view value);

/** @brief Render a WindowsSecretBackend back to its canonical CLI value string. */
std::string_view ToCliValue(WindowsSecretBackend backend);
```

CLI value mapping (fixed, do not rename):

| Enum value | CLI value |
|---|---|
| `WindowsSecretBackend::dpapi_user` | `dpapi-user` |
| `WindowsSecretBackend::dpapi_machine` | `dpapi-machine` |
| `WindowsSecretBackend::wincred` | `wincred` |

`ToCliValue` exists so error messages and diagnostic output (Task 05.0) can
render the enum back to the exact string users typed, without a second
mapping table living elsewhere.

## Constraints

- No adapter construction here - this subtask only produces the enum and its
  parser/renderer.
- `ParseWindowsSecretBackend` must not accept or special-case Linux backend
  names; recognizing and rejecting them with a helpful message is Subtask 03.
- Deterministic, side-effect-free parsing: no I/O, no environment lookups, no
  logging.

## Success criteria

- [ ] `WindowsSecretBackend` has exactly three enumerators:
      `dpapi_user`, `dpapi_machine`, `wincred`.
- [ ] `ParseWindowsSecretBackend("dpapi-user")`,
      `ParseWindowsSecretBackend("dpapi-machine")`, and
      `ParseWindowsSecretBackend("wincred")` return the matching enumerator.
- [ ] `ToCliValue` is the exact inverse of `ParseWindowsSecretBackend` for all
      three values.
- [ ] An unrecognized value throws `std::runtime_error` with a message
      listing all three valid Windows values.
