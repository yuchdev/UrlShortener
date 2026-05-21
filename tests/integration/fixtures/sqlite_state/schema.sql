CREATE TABLE short_links (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    short_code TEXT NOT NULL UNIQUE,
    target_url TEXT NOT NULL,
    created_by_user_id INTEGER,
    is_active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

CREATE TABLE analytics_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    short_code TEXT NOT NULL,
    event_type TEXT NOT NULL,
    ip_address TEXT,
    created_at TEXT NOT NULL
);
