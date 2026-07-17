# URL Shortener API Reference

Generated from `url_shortener::http::registeredRoutes()`. The C++ route
registry is the source of truth for HTTP API inventory, route labels, operation
IDs, compatibility markers, placeholders, and documentation grouping.

| Method | Path | Operation ID | Tags | Query params | Request body | Notes | Summary |
|---|---|---|---|---|---|---|---|
| GET | `/healthz` | `get_healthz` | Observability | - | - | - | Liveness probe; returns 200 when the process is running. |
| GET | `/readyz` | `get_readyz` | Observability | - | - | - | Readiness probe; returns 200 when the server can serve traffic. |
| GET | `/metrics` | `get_metrics` | Observability | - | - | - | Prometheus-style metrics exposition endpoint. |
| POST | `/api/v1/links` | `post_api_v1_links` | Links | - | JSON object containing target_url and optional slug, metadata, tags, campaign, expiry, and redirect type fields. | - | Create a short link from a target URL and optional attributes. |
| GET | `/api/v1/links/id/{id}` | `get_api_v1_links_id_id` | Links | - | - | - | Fetch a link by its opaque identifier. |
| GET | `/api/v1/links/{slug}` | `get_api_v1_links_slug` | Links | - | - | - | Fetch a link's full metadata by slug. |
| PATCH | `/api/v1/links/{slug}` | `patch_api_v1_links_slug` | Links | - | JSON object containing mutable link fields to update; null clears nullable fields where supported. | - | Partially update a link's mutable attributes. |
| DELETE | `/api/v1/links/{slug}` | `delete_api_v1_links_slug` | Links | - | - | - | Soft-delete a link by slug. |
| GET | `/api/v1/links/{slug}/preview` | `get_api_v1_links_slug_preview` | Links | - | - | - | Preview a link's resolved status without redirecting. |
| GET | `/api/v1/links/{slug}/qr` | `get_api_v1_links_slug_qr` | Links | - | - | placeholder | QR code generation placeholder (not yet implemented). |
| GET | `/api/v1/links/{slug}/routing` | `get_api_v1_links_slug_routing` | Links | - | - | placeholder | Routing rules placeholder (not yet implemented). |
| GET | `/api/v1/links/{slug}/stats` | `get_api_v1_links_slug_stats` | Links | `from`, `to`, `bucket` | - | - | Aggregate click statistics for a link. |
| POST | `/api/v1/links/{slug}/enable` | `post_api_v1_links_slug_enable` | Links | - | - | - | Enable a previously disabled link. |
| POST | `/api/v1/links/{slug}/disable` | `post_api_v1_links_slug_disable` | Links | - | - | - | Disable a link so it stops redirecting. |
| POST | `/api/v1/links/{slug}/restore` | `post_api_v1_links_slug_restore` | Links | - | - | - | Restore a soft-deleted link. |
| POST | `/api/v1/short-urls` | `post_api_v1_short_urls` | Compatibility | - | Compatibility JSON object containing url and optional code fields. | compatibility alias | Compatibility alias for creating a short link. |
| GET | `/api/v1/short-urls/{slug}` | `get_api_v1_short_urls_slug` | Compatibility | - | - | compatibility alias | Compatibility alias for fetching a link by slug. |
| GET | `/r/{slug}` | `get_r_slug` | Redirects | - | - | - | Prefixed redirect to a link's target URL. |
| GET | `/{slug}` | `get_slug` | Redirects | - | - | - | Root redirect to a link's target URL. |
| GET | `/{path}` | `get_path` | Fallback | - | - | - | Fallback URI store read for arbitrary application paths. |
| POST | `/{path}` | `post_path` | Fallback | - | Raw URI-store value to associate with the requested path. | - | Fallback URI store write for arbitrary application paths. |
| DELETE | `/{path}` | `delete_path` | Fallback | - | - | - | Fallback URI store delete for arbitrary application paths. |
| GET | `/` | `get` | Fallback | - | - | - | Fallback URI store read for the root application path. |
| POST | `/` | `post` | Fallback | - | Raw URI-store value to associate with the root path. | - | Fallback URI store write for the root application path. |
| DELETE | `/` | `delete` | Fallback | - | - | - | Fallback URI store delete for the root application path. |
