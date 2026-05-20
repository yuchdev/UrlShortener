INSERT INTO short_links (short_code, target_url, created_by_user_id, is_active, created_at)
VALUES ('abc123', 'https://example.com', 1, 1, '2024-01-01T00:00:00');

INSERT INTO analytics_events (short_code, event_type, ip_address, created_at)
VALUES ('abc123', 'created', '127.0.0.1', '2024-01-01T00:00:00');

INSERT INTO analytics_events (short_code, event_type, ip_address, created_at)
VALUES ('abc123', 'redirect', '192.168.1.1', '2024-01-01T00:01:00');

INSERT INTO analytics_events (short_code, event_type, ip_address, created_at)
VALUES ('abc123', 'redirect', '10.0.0.1', '2024-01-01T00:02:00');
