# Local Auth & External Clients

This document describes the authentication and authorization foundation for UrlShortener.

## Overview

The auth system is **local-only** — it is not an OAuth2 server and must never be exposed to the public internet. It provides:

- **Password-based authentication** (PBKDF2-SHA256 via OpenSSL)
- **Opaque session tokens** (SHA-256 hashed, never stored raw)
- **Role-based access control** via `ControlSet` (`admin` / `user`)
- **Immutable audit log** of all credential and token operations

## Control Sets

| ControlSet | Read | Write | Migrate | Manage Users |
|------------|------|-------|---------|--------------|
| `admin`    | ✓    | ✓     | ✓       | ✓            |
| `user`     | ✓    | ✗     | ✗       | ✗            |

## First Admin Bootstrap

The first admin user is created via `FirstAdminBootstrap`. This is the **only** path that can create an admin without prior authentication.

```cpp
// C++ example
UserCredentialsRepository users(db);
PasswordHasher             hasher;
AuthAuditLogRepository     audit(db);
FirstAdminBootstrap        bootstrap(users, hasher, audit);

auto admin = bootstrap.createFirstAdmin("admin", "strong-password");
```

Calling `createFirstAdmin` a second time throws `AdminAlreadyExistsError`.

## Authentication Flow

```cpp
// C++ example
AuthBrokerService broker(users, sessions, hasher, tokenGen, audit);

// Login
auto result = broker.createSession("alice", "password");
if (result.ok) {
    // Return result.rawToken to the caller — never log it
    std::string token = result.rawToken;
}

// Validate token on subsequent requests
auto intro = broker.introspectToken(token);
if (intro.active) {
    AccessGuard guard = AuthBrokerService::guardFor(*intro.user);
    guard.requireWrite(); // throws AccessDeniedError if not admin
}

// Revoke
broker.revokeToken(token);
```

### JavaScript example

```javascript
// Login
const res = await fetch('/auth/session', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ username: 'alice', password: 'secret' }),
});
const { token } = await res.json();

// Authenticated request
await fetch('/api/links', {
  headers: { 'Authorization': `Bearer ${token}` },
});
```

### Python example

```python
import requests

# Login
r = requests.post('/auth/session', json={'username': 'alice', 'password': 'secret'})
token = r.json()['token']

# Authenticated request
requests.get('/api/links', headers={'Authorization': f'Bearer {token}'})
```

## Security Invariants

- Passwords are **never** stored in plaintext; always hashed with PBKDF2-SHA256 (100 000 iterations, 16-byte random salt).
- Raw tokens are **never** stored; only their SHA-256 hash is persisted.
- Failed login attempts return a generic error — the response does **not** indicate whether the username or password was wrong.
- All credential and token operations are recorded in `auth_audit_log`.
- The audit log never stores passwords, hashes, raw tokens, or client secrets.

## Database Tables

| Table             | Purpose                                         |
|-------------------|-------------------------------------------------|
| `app_users`       | User credential records                         |
| `auth_sessions`   | Active/expired opaque session tokens            |
| `service_clients` | Local service clients allowed to call the broker|
| `auth_audit_log`  | Immutable record of all auth operations         |

See [migrations](../../db/migrations/postgres/002_add_users_auth_and_access_control.up.sql) for the full schema.
