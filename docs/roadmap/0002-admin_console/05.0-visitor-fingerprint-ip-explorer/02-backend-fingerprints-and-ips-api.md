# 02 - Backend Fingerprints and IPs API

**Parent task:** 05.0 Visitor, Fingerprint, and IP Explorer
**State:** ⬜ Not started
**Depends on:** none (parallel with subtask 01; both build on task 02.0)
**Blocks:** 04

## Objective

Implement the fingerprint and IP-address listing, detail, and related-object
endpoints, including the IP masking-policy hook.

## Files to add

```text
src/admin/controllers/FingerprintsController.h
src/admin/controllers/FingerprintsController.cpp
src/admin/controllers/IpAddressesController.h
src/admin/controllers/IpAddressesController.cpp
src/admin/storage/FingerprintsRepository.h
src/admin/storage/FingerprintsRepository.cpp
src/admin/storage/IpAddressesRepository.h
src/admin/storage/IpAddressesRepository.cpp
src/admin/dto/FingerprintDto.h
src/admin/dto/FingerprintDto.cpp
src/admin/dto/IpAddressDto.h
src/admin/dto/IpAddressDto.cpp
```

## API contract

```http
GET /admin/api/v1/fingerprints
GET /admin/api/v1/fingerprints/{fingerprint_id}
GET /admin/api/v1/fingerprints/{fingerprint_id}/events
GET /admin/api/v1/fingerprints/{fingerprint_id}/ips
GET /admin/api/v1/fingerprints/{fingerprint_id}/regeneration-history

GET /admin/api/v1/ips
GET /admin/api/v1/ips/{ip_id}
GET /admin/api/v1/ips/{ip_id}/events
GET /admin/api/v1/ips/{ip_id}/fingerprints
GET /admin/api/v1/ips/{ip_id}/visitor-profiles
```

## Requirements

1. Fingerprint list columns (
   [01-product-and-ux-spec.md §7.4](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):
   `Fingerprint ID, Provider, First Seen, Last Seen, Redirects, Unique
   Links, Unique IPs, Visitor Profile, Regeneration Attempts, Risk Score`.
   Detail adds: normalized fingerprint summary, provider confidence, smart
   signals, associated visitor profile, IP history, redirect history,
   regeneration history, related fingerprints, suspicious signals - **never**
   raw components by default (
   [01-product-and-ux-spec.md §7.4](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md),
   [06-security-privacy-permissions.md §6](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)).
2. IP list columns (
   [01-product-and-ux-spec.md §7.5](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):
   `IP or Masked IP, First Seen, Last Seen, Redirects, Unique Links, Unique
   Fingerprints, Visitor Profiles, Blocked Events, Risk Score`. Detail adds
   IP summary, associated fingerprints/visitor profiles, redirect timeline,
   rate-limit events, suspicious events, user-agent distribution, top
   clicked links.
3. `FingerprintDto`/`IpAddressDto` map from
   [05-data-model-and-storage.md §3, §5](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md).
4. IP display policy (
   [06-security-privacy-permissions.md §5](/docs/roadmap/0002-admin_console/06-security-privacy-permissions.md)):
   admin with `ips:read_full` -> full IP if storage policy allows; admin
   without it -> masked; read-only -> always masked. Implement this as a
   narrow policy check in `IpAddressesController` that task 08.0's
   `MaskingService`/`PrivacyPolicy` can later replace without changing the
   controller's public shape.
5. Enforce `fingerprints:read` / `ips:read` (and `ips:read_full` for
   unmasked IP).

## Constraints

- Provider-specific raw JSON must never leak into `FingerprintDto` - only
  the normalized envelope/signal fields defined in
  [03-fingerprint-and-suspicion-model.md §6](/docs/roadmap/0002-admin_console/03-fingerprint-and-suspicion-model.md)
  (
  [plan.md - Shared contract C1](/docs/roadmap/0002-admin_console/plan.md)).
- All raw/full sensitive fields must be entirely absent from the response
  when not allowed - not present-but-null, which would leak that the data
  exists.

## Success criteria

- [ ] `GET /fingerprints` and `GET /ips` are paginated, sortable, and
      searchable.
- [ ] Fingerprint detail never includes raw canvas/audio/font/WebGL
      components.
- [ ] IP responses are masked for read-only and for admin-without-
      `ips:read_full`, and full for admin-with-`ips:read_full` (when storage
      policy allows).
- [ ] `fingerprints:read` / `ips:read` are enforced on every endpoint.
