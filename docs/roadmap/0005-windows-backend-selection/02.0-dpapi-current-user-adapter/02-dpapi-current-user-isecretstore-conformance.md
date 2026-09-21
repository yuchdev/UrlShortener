# 02 - DPAPI CurrentUser ISecretStore conformance

**Parent task:** 02.0 DPAPI CurrentUser adapter
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Make `DpapiCurrentUserSecretStore` implement the `ISecretStore` interface
(see plan.md's verification note - this interface was not confirmed to
already exist in this repository; treat it as pre-existing if it turns out
to be, otherwise this subtask's scope includes defining it), so
`BuildWindowsSecretStore` (Task 05.0) can return it as
`std::unique_ptr<ISecretStore>` and command handlers can use it without
knowing which backend was selected.

## Files

- `include/url_shortener/secret_store/windows/dpapi_current_user_secret_store.hpp`
  (add `: public ISecretStore` and the interface method overrides)
- `src/secret_store/windows/dpapi_current_user_secret_store.cpp`

## Behavior

- Implement every `ISecretStore` method (create/read/update/delete, matching
  whatever the interface defines - confirm its actual shape first per the
  verification note in plan.md) in terms of `Protect`/`Unprotect` from
  Subtask 01 plus whatever persistence mechanism the `ISecretStore` contract
  expects (e.g. writing the DPAPI ciphertext blob to the same on-disk
  location the Linux backends use, if the interface is
  storage-location-agnostic; if `ISecretStore` owns persistence and only
  delegates protect/unprotect, wire accordingly - follow whatever
  `ISecretStore` actually declares once its real shape is confirmed).
- Preserve existing create/read/update/delete command behavior/semantics -
  this adapter must be a drop-in `ISecretStore` implementation, not a new
  command surface.

## Constraints

- Do not widen or change `ISecretStore` itself in this milestone - if the
  interface is missing something this adapter needs, flag it rather than
  editing the shared interface as a side effect of a Windows-only change.
- No secret material in any exception thrown across the `ISecretStore`
  boundary (plan.md C3).

## Success criteria

- [ ] `DpapiCurrentUserSecretStore` satisfies `ISecretStore` and compiles
      behind a `std::unique_ptr<ISecretStore>` pointer.
- [ ] Existing create/read/update/delete command behavior is unchanged when
      backed by this adapter (verified via Task 06.0 fake-adapter and
      real-adapter tests).
