CREATE TABLE IF NOT EXISTS click_events (
  event_id TEXT PRIMARY KEY,
  occurred_at TIMESTAMPTZ NOT NULL,
  slug TEXT NOT NULL,
  link_id TEXT NOT NULL,
  domain TEXT NOT NULL,
  status_code INTEGER NOT NULL,
  referrer TEXT,
  user_agent TEXT,
  client_id_hash TEXT
);
CREATE INDEX IF NOT EXISTS idx_click_events_slug_occurred_at ON click_events(slug, occurred_at);
CREATE INDEX IF NOT EXISTS idx_click_events_slug_status_code ON click_events(slug, status_code);
CREATE INDEX IF NOT EXISTS idx_click_events_slug_domain ON click_events(slug, domain);
