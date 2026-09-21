# 01 - Backend Privacy and Masking

**Parent task:** 08.0 Exports, Privacy, and Permissions
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 02

## Objective

Implement the centralized `PrivacyPolicy` and `MaskingService` that every
admin controller routes its sensitive-field decisions through, then rewire
task 05.0's IP-masking hook (and any other ad hoc masking check introduced
by tasks 03.0-07.0) to call it.

## Files to add

```text
src/admin/privacy/PrivacyPolicy.h
src/admin/privacy/PrivacyPolicy.cpp
src/admin/privacy/MaskingService.h
src/admin/privacy/MaskingService.cpp
```

## Requirements

1. `PrivacyPolicy` centralizes the rules from
   [06-security-privacy-permissions.md §5-6](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md):

   ```text
   admin + ips:read_full -> full IP if storage policy allows
   admin without ips:read_full -> masked IP
   readonly -> masked IP
   raw fingerprint components: hidden by default for every role
   ```

2. `MaskingService` provides the actual field-transformation functions
   (`maskIp`, `stripRawFingerprintComponents`, etc.) that controllers call
   with a `(value, currentUser)` pair and get back either the real value or
   a masked/absent one - fields must be **absent**, not null, when not
   allowed (matching task 05.0 subtask 02's constraint).
3. Rewire `IpAddressesController` (task 05.0 subtask 02) and any other
   controller with an inline masking check to call `MaskingService` instead
   of a local policy check.
4. Support a privacy-policy config hook, retention settings, and a raw-
   payload disable switch as configurable inputs to `PrivacyPolicy` (
   [06-security-privacy-permissions.md §11](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)),
   reusing `FingerprintConfig`'s `store_raw_debug_payloads` flag (task 06.0
   subtask 01) rather than duplicating it.

## Constraints

- `MaskingService` must be the single place IP/fingerprint masking logic
  lives - no controller should reimplement the admin+`ips:read_full` vs.
  read-only branching after this subtask lands.

## Success criteria

- [ ] `PrivacyPolicy`/`MaskingService` implement the exact IP and
      fingerprint masking matrix from
      `06-security-privacy-permissions.md §5-6`.
- [ ] `IpAddressesController` (task 05.0) is rewired to call
      `MaskingService` and produces identical output to before the rewire
      for equivalent inputs.
- [ ] Masked-out fields are absent from JSON responses, not null.
