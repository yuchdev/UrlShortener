# Task 05 — Visitor, Fingerprint, and IP Explorer

## Goal

Implement safe browsing and detail pages for visitor profiles, fingerprints, and IP addresses.

This task makes the identity graph visible to admins without exposing raw fingerprint data unnecessarily.

## Required frontend files

Create:

```text
web/admin/src/api/visitorProfilesApi.ts
web/admin/src/api/fingerprintsApi.ts
web/admin/src/api/ipsApi.ts
web/admin/src/model/visitorProfile.ts
web/admin/src/model/fingerprint.ts
web/admin/src/model/ipAddress.ts
web/admin/src/pages/VisitorProfilesPage.tsx
web/admin/src/pages/VisitorProfileDetailPage.tsx
web/admin/src/pages/FingerprintsPage.tsx
web/admin/src/pages/FingerprintDetailPage.tsx
web/admin/src/pages/IpAddressesPage.tsx
web/admin/src/pages/IpAddressDetailPage.tsx
web/admin/src/components/privacy/MaskedIp.tsx
web/admin/src/components/privacy/FingerprintId.tsx
web/admin/src/components/privacy/SensitiveValue.tsx
web/admin/src/components/analytics/FingerprintSignalsPanel.tsx
web/admin/src/components/analytics/RiskReasonsList.tsx
web/admin/src/components/analytics/IdentityTimeline.tsx
```

## Required backend files

Create:

```text
src/admin/controllers/VisitorProfilesController.h
src/admin/controllers/VisitorProfilesController.cpp
src/admin/controllers/FingerprintsController.h
src/admin/controllers/FingerprintsController.cpp
src/admin/controllers/IpAddressesController.h
src/admin/controllers/IpAddressesController.cpp
src/admin/storage/VisitorProfilesRepository.h
src/admin/storage/VisitorProfilesRepository.cpp
src/admin/storage/FingerprintsRepository.h
src/admin/storage/FingerprintsRepository.cpp
src/admin/storage/IpAddressesRepository.h
src/admin/storage/IpAddressesRepository.cpp
src/admin/dto/VisitorProfileDto.h
src/admin/dto/VisitorProfileDto.cpp
src/admin/dto/FingerprintDto.h
src/admin/dto/FingerprintDto.cpp
src/admin/dto/IpAddressDto.h
src/admin/dto/IpAddressDto.cpp
```

## Required endpoints

```http
GET /admin/api/v1/visitor-profiles
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/events
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/fingerprints
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/ips

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

## Visitor profile table columns

```text
Visitor Profile ID
Primary Fingerprint
Known IPs
Known Fingerprints
Redirects
First Seen
Last Seen
Risk
```

## Fingerprint table columns

```text
Fingerprint ID
Provider
Visitor Profile
First Seen
Last Seen
Redirects
IPs
Regeneration Attempts
Risk
```

## IP table columns

```text
IP / Masked IP
First Seen
Last Seen
Redirects
Fingerprints
Visitor Profiles
Blocked Events
Risk
```

## Detail-page requirements

Each detail page must include:

```text
summary cards
risk score and risk reasons
related objects
activity timeline
recent redirect events
suspicious events
```

Fingerprint detail additionally shows:

```text
provider
provider confidence
smart signals
regeneration history
```

IP detail additionally shows:

```text
full or masked IP according to permission
associated fingerprints
associated visitor profiles
user-agent distribution
rate-limit events
```

## Privacy requirements

- Raw fingerprint components are never shown by default.
- Backend decides whether IP is full or masked.
- Read-only users see masked IP values.
- All raw/full sensitive fields must be absent from response if not allowed.

## Tests to add

Frontend:

```text
web/admin/src/pages/VisitorProfilesPage.test.tsx
web/admin/src/pages/FingerprintsPage.test.tsx
web/admin/src/pages/IpAddressesPage.test.tsx
web/admin/src/components/privacy/MaskedIp.test.tsx
web/admin/src/components/analytics/FingerprintSignalsPanel.test.tsx
```

Backend:

```text
tests/unit/admin/VisitorProfilesRepositoryTest.cpp
tests/unit/admin/FingerprintsRepositoryTest.cpp
tests/unit/admin/IpAddressesRepositoryTest.cpp
tests/integration/admin/VisitorProfilesApiTest.cpp
tests/integration/admin/FingerprintsApiTest.cpp
tests/integration/admin/IpAddressesApiTest.cpp
tests/integration/admin/ReadOnlyIpMaskingTest.cpp
```

## Exit criteria

- Visitor profiles are paginated and searchable.
- Fingerprints are paginated and searchable.
- IP addresses are paginated and searchable.
- Detail pages show related objects and recent events.
- Read-only users see masked IPs.
- Raw fingerprint components are not exposed.
- Backend tests verify masking behavior.
