# Stage 04 — Analytics/Event Pipeline Split by Implementation Step

This folder replaces the previous document-type split. Each document is an implementation step and contains its own:

- files to create/modify
- classes/types to implement
- C++ unit tests
- C++ integration tests
- Python integration tests where applicable
- documentation updates
- exit criteria

## Files

1. `04.1-STAGE_ANALYTICS_CORE_EVENT_MODEL.md` — event model, config, sanitization, hashing, aggregate DTOs.
2. `04.2-STAGE_ANALYTICS_ASYNC_QUEUE_AND_REDIRECT_HOOK.md` — bounded queue, service, worker, metrics, redirect hook.
3. `04.3-STAGE_ANALYTICS_STORAGE_AND_STATS_ENDPOINT.md` — repositories, SQL migration, aggregation, stats API.
4. `04.4-STAGE_ANALYTICS_RETENTION_PRIVACY_CI_AND_PERFORMANCE.md` — retention, runtime config, docs, CI, performance checks.

Recommended execution order: 04.1 → 04.2 → 04.3 → 04.4.
