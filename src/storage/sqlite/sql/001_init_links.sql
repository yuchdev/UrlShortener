CREATE TABLE IF NOT EXISTS links (
  id TEXT NOT NULL,
  short_code TEXT PRIMARY KEY,
  target_url TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  expires_at INTEGER NULL,
  is_active INTEGER NOT NULL,
  owner_id TEXT NULL,
  attributes_json TEXT NULL
);
