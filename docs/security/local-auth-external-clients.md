# Local-Only Auth: External Client Integration Guide

This document describes the authentication and authorization foundation for UrlShortener
and how external applications running on the same server can integrate with it.

## Architecture

```text
Web App A ─┐
Web App B ─┼── Unix socket / localhost API ── urlshortener-authd ── database
CLI Tool  ─┤
GUI App   ─┘
```

The auth system is **local-only** and must never be exposed to the public internet.

External applications must:
- Authenticate through the local auth broker (`urlshortener-authd`)
- Never read the `app_users` table directly
- Never receive password hashes

## Control Sets

| ControlSet | Read | Write | Migrate | Manage Users |
|------------|------|-------|---------|--------------|
| `admin`    | ✓    | ✓     | ✓       | ✓            |
| `user`     | ✓    | ✗     | ✗       | ✗            |

## Transport

### Preferred: Unix Domain Socket

```text
/run/urlshortener/auth.sock
owner: urlshortener-auth
group: urlshortener-webapps
mode:  0660
```

Only processes in the `urlshortener-webapps` group may connect.

### Fallback: Localhost HTTP

```text
127.0.0.1:<configured_port>   (IPv4 loopback)
[::1]:<configured_port>        (IPv6 loopback)
```

The service must **never** bind to `0.0.0.0`.

## Auth Broker API

### POST /v1/auth/sessions — Create Session

**Request:**
```json
{ "username": "alice", "password": "plain-password" }
```

**Success response:**
```json
{
  "ok": true,
  "access_token": "opaque-random-token",
  "expires_at": "2026-05-20T15:30:00Z",
  "user": { "id": "...", "username": "alice", "control_set": "user" }
}
```

**Failure response:**
```json
{ "ok": false, "error": "invalid_credentials" }
```

### POST /v1/auth/tokens/introspect — Inspect Token

**Request:**
```json
{ "access_token": "opaque-random-token" }
```

**Active token response:**
```json
{
  "active": true,
  "user": { "id": "...", "username": "alice", "control_set": "user" },
  "permissions": { "read": true, "write": false, "migrate": false, "manage_users": false },
  "expires_at": "2026-05-20T15:30:00Z"
}
```

**Inactive token response:**
```json
{ "active": false }
```

### POST /v1/auth/tokens/revoke — Revoke Token

**Request:**
```json
{ "access_token": "opaque-random-token" }
```

**Response:**
```json
{ "ok": true }
```

---

## Example: JavaScript / Node.js

The preferred approach uses a Unix domain socket to communicate with the auth broker.

```js
import http from "node:http";

function postJsonOverUnixSocket(path, payload) {
  const body = JSON.stringify(payload);

  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        socketPath: "/run/urlshortener/auth.sock",
        path,
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(body)
        }
      },
      (res) => {
        let data = "";
        res.setEncoding("utf8");
        res.on("data", chunk => { data += chunk; });
        res.on("end", () => {
          try {
            resolve(JSON.parse(data));
          } catch (err) {
            reject(err);
          }
        });
      }
    );

    req.on("error", reject);
    req.write(body);
    req.end();
  });
}

export async function createSession(username, password) {
  return postJsonOverUnixSocket("/v1/auth/sessions", {
    username,
    password
  });
}

export async function introspectToken(accessToken) {
  return postJsonOverUnixSocket("/v1/auth/tokens/introspect", {
    access_token: accessToken
  });
}
```

> **Warning:** Never log `password` or `access_token`. Never pass passwords in URLs.
> Never read the `app_users` table directly from Node.js web apps.

---

## Example: Python

Use the standard-library `socket` module to talk to the auth broker over a Unix domain socket.

```python
import json
import socket

SOCKET_PATH = "/run/urlshortener/auth.sock"


def post_json(path: str, payload: dict) -> dict:
    body = json.dumps(payload).encode("utf-8")

    request = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: urlshortener-authd\r\n"
        f"Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode("utf-8") + body

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(SOCKET_PATH)
        client.sendall(request)
        response = b""
        while True:
            chunk = client.recv(4096)
            if not chunk:
                break
            response += chunk

    _, _, raw_body = response.partition(b"\r\n\r\n")
    return json.loads(raw_body.decode("utf-8"))


def create_session(username: str, password: str) -> dict:
    return post_json("/v1/auth/sessions", {
        "username": username,
        "password": password,
    })


def introspect_token(access_token: str) -> dict:
    return post_json("/v1/auth/tokens/introspect", {
        "access_token": access_token,
    })
```

> **Warning:** Do not print returned access tokens in production logs.
> Do not store passwords in configuration files.
> Prefer environment variables, secret stores, or interactive input.

---

## Example: C++

The following shows how to make a raw HTTP/1.1 POST over a Unix domain socket.

```cpp
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>

std::string postJsonOverUnixSocket(
    const std::string& socketPath,
    const std::string& path,
    const std::string& jsonBody
) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("failed to create Unix socket");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("failed to connect to auth socket");
    }

    std::string request =
        "POST " + path + " HTTP/1.1\r\n"
        "Host: urlshortener-authd\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + jsonBody;

    ssize_t written = ::write(fd, request.data(), request.size());
    if (written < 0) {
        ::close(fd);
        throw std::runtime_error("failed to write request");
    }

    std::string response;
    char buffer[4096];
    while (true) {
        ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            ::close(fd);
            throw std::runtime_error("failed to read response");
        }
        if (n == 0) {
            break;
        }
        response.append(buffer, static_cast<size_t>(n));
    }

    ::close(fd);
    return response;
}
```

> **Warning:** This example shows transport only. Production code must parse HTTP status codes,
> validate JSON, handle timeouts, and avoid logging `password` or `access_token` values.

---

## In-Process C++ Usage (same binary)

If your code runs inside the UrlShortener process, use the C++ API directly:

```cpp
// First-time bootstrap (only when no active admin exists)
UserCredentialsRepository users(db);
PasswordHasher             hasher;
AuthAuditLogRepository     audit(db);
FirstAdminBootstrap        bootstrap(users, hasher, audit);

auto admin = bootstrap.createFirstAdmin("admin", "strong-password");
// admin.passwordHash is populated but must never be printed or logged

// Authenticate and create a session
AuthSessionRepository sessions(db);
TokenGenerator        tokenGen;
AuthBrokerService     broker(users, sessions, hasher, tokenGen, audit);

auto result = broker.createSession("alice", "password");
if (result.ok) {
    const std::string& token = result.rawToken; // return to caller; do not store; do not log
}

// Validate a token on subsequent requests
auto intro = broker.introspectToken(token);
if (intro.active) {
    AccessGuard guard = AuthBrokerService::guardFor(*intro.user);
    guard.requireWrite(); // throws AccessDeniedError if not admin
}

// Revoke when done
broker.revokeToken(token);
```

## Security Invariants

1. Passwords are **never** stored in plaintext — hashed with PBKDF2-SHA256 (100,000 iterations, 16-byte random salt).
2. Raw tokens are **never** stored — only their SHA-256 hash is persisted.
3. Failed login attempts return a generic error — the response does **not** indicate whether the username or password was wrong.
4. All credential and token operations are recorded in `auth_audit_log`.
5. The audit log never stores passwords, hashes, raw tokens, or client secrets.
6. The auth service binds only to a Unix socket or loopback interface.

## Database Tables

| Table             | Purpose                                          |
|-------------------|--------------------------------------------------|
| `app_users`       | User credential records                          |
| `auth_sessions`   | Active/expired opaque session tokens             |
| `service_clients` | Local service clients allowed to call the broker |
| `auth_audit_log`  | Immutable record of all auth operations          |

See [PostgreSQL migration](../../db/migrations/postgres/002_add_users_auth_and_access_control.up.sql)
and [SQLite migration](../../src/storage/sql/migrations/005_auth_tables.sql) for the full schema.
