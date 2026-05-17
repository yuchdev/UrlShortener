# How to Add a New Adapter

Checklist:

1. Implement the relevant storage interface(s).
2. Map backend errors into shared error taxonomy.
3. Register backend in config parser/factory selection.
4. Wire metadata contract tests for the new adapter.
5. Add backend-specific unit tests.
6. Add integration tests (including failure-path behavior).
7. Update storage docs and CI matrix job runners.
