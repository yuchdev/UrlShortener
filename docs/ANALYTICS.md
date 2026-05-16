# Analytics Domain

## Event model

`ClickEvent` represents one redirect attempt with normalized fields: event id, timestamp, slug/link id, domain, status code, optional referrer/user-agent strings, and anonymized `client_id_hash`.

The model intentionally does not include any raw client IP field.

## Sanitization limits

Default limits:
- referrer: 512 bytes
- user-agent: 512 bytes
- domain: 255 bytes

Sanitization removes ASCII control characters, trims surrounding whitespace, and deterministically truncates by byte length.

## Client hash derivation

`ClientIdHasher` derives `client_id_hash` by keyed HMAC-SHA256 over a normalized network identifier using a configured salt.
Identical `(identifier, salt)` pairs produce stable hashes; salt rotation changes output.

## Stage 04.3 stats endpoint

- Endpoint: `GET /api/v1/links/{slug}/stats?from=<epoch_seconds>&to=<epoch_seconds>&bucket=hour|day|week`.
- Unknown slug policy: returns `200` with zero-count aggregates.
- Storage: analytics click events are stored in `click_events` with indexes on `(slug, occurred_at)`, `(slug, status_code)`, `(slug, domain)`.

