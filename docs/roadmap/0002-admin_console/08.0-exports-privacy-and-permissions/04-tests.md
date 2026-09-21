# 04 - Tests

**Parent task:** 08.0 Exports, Privacy, and Permissions
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** none

## Objective

Cover the masking service, export policy, export API, and frontend export
UI, including the read-only-cannot-export-raw-events case and the export
audit-log assertion.

## Files to add

Frontend:

```text
web/admin/src/components/admin/ExportDialog.test.tsx
web/admin/src/components/admin/ExportButton.test.tsx
```

Backend:

```text
tests/unit/admin/ExportPolicyTest.cpp
tests/unit/admin/MaskingServiceTest.cpp
tests/unit/admin/PrivacyPolicyTest.cpp
tests/integration/admin/ExportApiTest.cpp
tests/integration/admin/ReadOnlyCannotExportRawEventsTest.cpp
tests/integration/admin/ExportAuditLogTest.cpp
```

## Requirements

1. `ExportPolicyTest` covers every role/permission combination from task
   08.0 subtask 02's permission-rule table.
2. `MaskingServiceTest` / `PrivacyPolicyTest` cover the IP/fingerprint
   masking matrix from subtask 01, including the "absent, not null" field
   behavior.
3. `ExportApiTest` covers all four export types succeeding for an
   appropriately-permissioned caller.
4. `ReadOnlyCannotExportRawEventsTest` is a dedicated integration test
   asserting a read-only session cannot create a `redirect_events_*` export
   regardless of other permissions.
5. `ExportAuditLogTest` asserts an export request and its completion both
   appear in the audit log with correct fields.
6. Frontend tests cover `ExportDialog` hiding disallowed export types and
   `ExportButton` triggering the dialog correctly.

## Constraints

- Keep the read-only-raw-export-denial and audit-log tests isolated in
  their own files, matching the source task's explicit test list.

## Success criteria

- [ ] All backend tests above pass under CTest.
- [ ] All frontend tests above pass under `npm run test`.
- [ ] Read-only user cannot export raw redirect events (backend-enforced).
- [ ] Every export action is visible in the audit log (asserted by
      `ExportAuditLogTest`).
- [ ] Exported IP/fingerprint data is masked as required in at least one
      integration test assertion.
