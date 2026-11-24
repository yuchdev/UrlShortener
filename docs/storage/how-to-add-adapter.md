# How to Add a New Adapter

Checklist:

1. Implement the relevant storage interface(s). For `IMetadataRepository` all
   seven methods must be provided: `CreateLink`, `GetByShortCode`, `GetById`
   (indexed lookup by stable id — SQL backends require an index on the `id`
   column analogous to migration `005_index_links_id`; a plain `CREATE INDEX`
   is write-blocking on large Postgres tables and should be run in a
   maintenance window), `UpdateLink`, `DeleteLink`, `ListLinks`, and `Exists`.
2. Map backend errors into shared error taxonomy.
3. Register backend in config parser/factory selection.
4. Wire metadata contract tests for the new adapter.
5. Add backend-specific unit tests.
6. Add integration tests (including failure-path behavior).
7. Update storage docs and CI matrix job runners.
