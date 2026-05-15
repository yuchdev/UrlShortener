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
