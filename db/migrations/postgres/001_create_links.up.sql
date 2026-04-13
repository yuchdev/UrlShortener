CREATE TABLE IF NOT EXISTS links (
    id VARCHAR NOT NULL,
    short_code VARCHAR PRIMARY KEY,
    target_url TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NULL,
    is_active BOOLEAN NOT NULL,
    owner_id VARCHAR NULL,
    attributes_json JSONB NULL
);
