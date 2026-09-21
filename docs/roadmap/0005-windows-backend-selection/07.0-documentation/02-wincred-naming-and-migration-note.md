# 02 - wincred naming and migration note

**Parent task:** 07.0 Documentation
**State:** ⬜ Not started
**Depends on:** none (documents Task 04.0, 05.0 once implemented)
**Blocks:** none

## Objective

Publish the Credential Manager naming rules and the migration note for users
currently relying on automatic Windows backend selection - the remaining two
items of plan.md's "Documentation updates" list.

## Files

- `docs/security/windows-secret-backends.md` (extend the file created in
  Subtask 01)

## Content

### Credential Manager naming rules

- Target name format: `<application-namespace>:<secret-key>` (Task 04.0
  Subtask 01) - document the exact namespace string once implementation
  fixes it, and give at least one worked example target name.
- Note that `wincred` entries are visible to the user through Windows'
  built-in Credential Manager UI (`Control Panel > Credential Manager`),
  unlike the DPAPI-backed entries - call this out explicitly since it means
  users can see (though not read the underlying value of) these entries
  outside the CLI.
- Document the credential-blob size limit
  (`CRED_MAX_CREDENTIAL_BLOB_SIZE`) and that oversized values are rejected
  at write time with a clear error (Task 04.0 Subtask 01).

### Migration note

- For users who previously ran `secret-store.exe` on Windows with no
  `--backend` flag and relied on whatever automatic detection existed
  before this milestone: after this change, omitting `--backend` resolves
  explicitly to `dpapi-user` (Task 05.0 Subtask 02). Call out that this is
  now an explicit, documented default rather than opaque auto-detection, and
  that existing secrets stored under the prior auto-selected backend (if it
  differed from `dpapi-user`) will not be automatically migrated - users
  who need `dpapi-machine` or `wincred` behavior must now pass `--backend`
  explicitly.
- Cross-reference plan.md C2 (no silent fallback) so readers understand why
  an invalid or platform-inappropriate `--backend` value now fails instead
  of quietly picking something else.

## Constraints

- Describe only behavior implemented and tested by Task 04.0 and Task 05.0 -
  do not speculate about a migration/backfill tool this milestone does not
  build.

## Success criteria

- [ ] `docs/security/windows-secret-backends.md` includes the Credential
      Manager naming-rule section and the migration note.
- [ ] The migration note accurately reflects the implemented default
      (verified against Task 05.0 once complete).
