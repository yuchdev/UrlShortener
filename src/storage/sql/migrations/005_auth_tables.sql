-- SQLite-compatible auth tables migration
-- SQLite does not support native roles; permission enforcement is done in the application layer.

CREATE TABLE IF NOT EXISTS app_users (
    id            TEXT PRIMARY KEY,
    username      TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    control_set   TEXT NOT NULL CHECK (control_set IN ('admin', 'user')),
    is_active     INTEGER NOT NULL DEFAULT 1,
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL,
    last_login_at INTEGER NULL
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_app_users_username
    ON app_users(username);

CREATE INDEX IF NOT EXISTS idx_app_users_control_set
    ON app_users(control_set);

CREATE TABLE IF NOT EXISTS auth_sessions (
    id           TEXT PRIMARY KEY,
    user_id      TEXT NOT NULL,
    client_id    TEXT NULL,
    token_hash   TEXT NOT NULL UNIQUE,
    created_at   INTEGER NOT NULL,
    expires_at   INTEGER NOT NULL,
    revoked_at   INTEGER NULL,
    last_used_at INTEGER NULL
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_auth_sessions_token_hash
    ON auth_sessions(token_hash);

CREATE INDEX IF NOT EXISTS idx_auth_sessions_user_id
    ON auth_sessions(user_id);

CREATE TABLE IF NOT EXISTS service_clients (
    id                 TEXT PRIMARY KEY,
    client_id          TEXT NOT NULL UNIQUE,
    client_secret_hash TEXT NULL,
    allowed_scopes     TEXT NOT NULL,
    is_active          INTEGER NOT NULL DEFAULT 1,
    created_at         INTEGER NOT NULL,
    updated_at         INTEGER NOT NULL,
    last_used_at       INTEGER NULL
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_service_clients_client_id
    ON service_clients(client_id);

CREATE TABLE IF NOT EXISTS auth_audit_log (
    id         TEXT PRIMARY KEY,
    client_id  TEXT NULL,
    username   TEXT NULL,
    user_id    TEXT NULL,
    action     TEXT NOT NULL,
    result     TEXT NOT NULL,
    reason     TEXT NULL,
    created_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_auth_audit_log_created_at
    ON auth_audit_log(created_at);
