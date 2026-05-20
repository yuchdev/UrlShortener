-- app_users: User credential records
CREATE TABLE IF NOT EXISTS app_users (
    id            VARCHAR NOT NULL,
    username      VARCHAR NOT NULL,
    password_hash VARCHAR NOT NULL,
    control_set   VARCHAR NOT NULL,
    is_active     BOOLEAN NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL,
    updated_at    TIMESTAMPTZ NOT NULL,
    last_login_at TIMESTAMPTZ NULL,
    PRIMARY KEY (id),
    UNIQUE (username),
    CHECK (control_set IN ('admin', 'user'))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_app_users_username
    ON app_users(username);

CREATE INDEX IF NOT EXISTS idx_app_users_control_set
    ON app_users(control_set);

-- auth_sessions: Opaque local access tokens
CREATE TABLE IF NOT EXISTS auth_sessions (
    id           VARCHAR NOT NULL,
    user_id      VARCHAR NOT NULL,
    client_id    VARCHAR NULL,
    token_hash   VARCHAR NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL,
    expires_at   TIMESTAMPTZ NOT NULL,
    revoked_at   TIMESTAMPTZ NULL,
    last_used_at TIMESTAMPTZ NULL,
    PRIMARY KEY (id),
    UNIQUE (token_hash)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_auth_sessions_token_hash
    ON auth_sessions(token_hash);

CREATE INDEX IF NOT EXISTS idx_auth_sessions_user_id
    ON auth_sessions(user_id);

-- service_clients: Local applications allowed to call auth broker
CREATE TABLE IF NOT EXISTS service_clients (
    id                 VARCHAR NOT NULL,
    client_id          VARCHAR NOT NULL,
    client_secret_hash VARCHAR NULL,
    allowed_scopes     VARCHAR NOT NULL,
    is_active          BOOLEAN NOT NULL DEFAULT TRUE,
    created_at         TIMESTAMPTZ NOT NULL,
    updated_at         TIMESTAMPTZ NOT NULL,
    last_used_at       TIMESTAMPTZ NULL,
    PRIMARY KEY (id),
    UNIQUE (client_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_service_clients_client_id
    ON service_clients(client_id);

-- auth_audit_log: Immutable record of credential and token operations
CREATE TABLE IF NOT EXISTS auth_audit_log (
    id         VARCHAR NOT NULL,
    client_id  VARCHAR NULL,
    username   VARCHAR NULL,
    user_id    VARCHAR NULL,
    action     VARCHAR NOT NULL,
    result     VARCHAR NOT NULL,
    reason     VARCHAR NULL,
    created_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (id)
);

CREATE INDEX IF NOT EXISTS idx_auth_audit_log_created_at
    ON auth_audit_log(created_at);

-- PostgreSQL role setup (documentation only; run manually if needed)
-- DO $$ BEGIN
--   IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'urlshortener_admin') THEN
--     CREATE ROLE urlshortener_admin;
--   END IF;
-- END $$;
-- GRANT ALL PRIVILEGES ON SCHEMA public TO urlshortener_admin;
-- GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO urlshortener_admin;
-- GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO urlshortener_admin;
