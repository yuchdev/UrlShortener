# 03 - Frontend Export and Privacy UI

**Parent task:** 08.0 Exports, Privacy, and Permissions
**State:** ⬜ Not started
**Depends on:** 02
**Blocks:** 04

## Objective

Build the export button/dialog used across list/analytics pages, a privacy
notice component, and complete the `SettingsPage` (a placeholder since task
01.0) with privacy/export-policy-relevant settings.

## Files to add

```text
web/admin/src/api/exportsApi.ts
web/admin/src/model/exportJob.ts
web/admin/src/components/admin/ExportButton.tsx
web/admin/src/components/admin/ExportDialog.tsx
web/admin/src/components/privacy/PrivacyNotice.tsx
web/admin/src/pages/SettingsPage.tsx
```

## Requirements

1. `ExportButton` + `ExportDialog` are generic enough to mount on the URL
   pairs page (task 04.0), the analytics dashboard (task 03.0), and any
   redirect-event table - they accept an export type and current filters,
   not a hardcoded page-specific payload.
2. `ExportDialog` reflects the permission rules from task 08.0 subtask 02:
   it must not offer an export type the current user's permissions
   (`exports:create_aggregate`, `exports:create_raw`) do not allow -
   `PermissionGate`-driven, backend-enforced regardless.
3. `PrivacyNotice` surfaces the privacy-policy text/config hook from
   [06-security-privacy-permissions.md §11](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md);
   mount it on pages that show identity data (visitor profiles,
   fingerprints, IPs - task 05.0) and near export controls.
4. `SettingsPage` replaces the task 01.0 placeholder with real content:
   retention settings display, masking/anonymization status, raw-payload
   disable switch status (read-only display is sufficient unless
   `settings:write` is granted, per
   [plan.md - Shared contract C6](/docs/roadmap/0002-admin_console/plan.md)).

## Constraints

- `ExportDialog` must poll or otherwise reflect `GET /exports/{export_id}`
  status (task 08.0 subtask 02) rather than assuming synchronous
  completion.

## Success criteria

- [ ] `ExportButton`/`ExportDialog` work from at least the URL pairs page
      and the dashboard.
- [ ] A read-only user without `exports:create_raw` never sees the raw
      redirect-events export option.
- [ ] `PrivacyNotice` renders on identity-data pages.
- [ ] `SettingsPage` shows real retention/masking/raw-payload policy state.
