# 01 - Backend Visitor Profiles API

**Parent task:** 05.0 Visitor, Fingerprint, and IP Explorer
**State:** ⬜ Not started
**Depends on:** none (builds on task 02.0's query primitives)
**Blocks:** 04

## Objective

Implement the visitor-profile listing, detail, and related-object endpoints.

## Files to add

```text
src/admin/controllers/VisitorProfilesController.h
src/admin/controllers/VisitorProfilesController.cpp
src/admin/storage/VisitorProfilesRepository.h
src/admin/storage/VisitorProfilesRepository.cpp
src/admin/dto/VisitorProfileDto.h
src/admin/dto/VisitorProfileDto.cpp
```

## API contract

```http
GET /admin/api/v1/visitor-profiles
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/events
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/fingerprints
GET /admin/api/v1/visitor-profiles/{visitor_profile_id}/ips
```

## Requirements

1. List columns (
   [01-product-and-ux-spec.md §7.3](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)):

   ```text
   Visitor Profile ID, First Seen, Last Seen, Redirects, Unique Links,
   Fingerprints, IPs, Regeneration Attempts, Risk Score
   ```

2. Detail response covers identity summary, associated fingerprints,
   associated IPs, clicked links, activity timeline, fingerprint changes,
   IP changes, risk indicators (
   [01-product-and-ux-spec.md §7.3](/docs/roadmap/0002-admin_console/01-product-and-ux-spec.md)).
3. `VisitorProfileDto` maps directly from the `visitor_profile` schema (
   [05-data-model-and-storage.md §6](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)):
   `id, primary_fingerprint_id, first_seen_at, last_seen_at, redirect_count,
   known_fingerprint_count, known_ip_count, regeneration_attempt_count,
   current_risk_score, current_risk_level`.
4. `/events`, `/fingerprints`, `/ips` sub-resources are paginated via task
   02.0 primitives and use the `redirect_event(visitor_profile_id,
   timestamp)` / `fingerprint(visitor_profile_id)` indexes (
   [05-data-model-and-storage.md §11](/docs/roadmap/0002-admin_console/05-data-model-and-storage.md)).
5. Enforce `visitor_profiles:read`.
6. `current_risk_score`/`current_risk_level` will read as low/zero until
   task 06.0 populates them - return the real stored column, not a
   hardcoded placeholder.

## Constraints

- Do not expose raw fingerprint components through this controller - that
  boundary belongs entirely to the fingerprints endpoints (subtask 02) and
  is off by default there too.

## Success criteria

- [ ] `GET /visitor-profiles` is paginated, sortable, and searchable.
- [ ] `GET /visitor-profiles/{id}` and its three sub-resources return
      correct data for a seeded visitor profile.
- [ ] `visitor_profiles:read` is enforced.
- [ ] Response DTO field names/types match `05-data-model-and-storage.md`
      exactly.
