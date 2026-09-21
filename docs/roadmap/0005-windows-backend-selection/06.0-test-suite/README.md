# Task 06.0 - Test suite

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Add the full Windows-focused test suite for this milestone: fake-adapter
unit tests covering parsing, validation, and factory selection (Tasks
01.0-05.0), a cross-cutting secret-safety assertion, and opt-in
Windows-only integration tests against the real DPAPI/Credential Manager
APIs.

This task is the single owner of every concrete test case listed in the
original plan (plan.md's "Tests" section, now distributed across the four
subtasks below); Tasks 01.0-05.0 implement production code without
re-stating these test cases so each requirement lands exactly once.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Parser and validation unit tests](01-parser-and-validation-unit-tests.md) | ⬜ Not started | none |
| 02 | [Factory adapter selection unit tests](02-factory-adapter-selection-unit-tests.md) | ⬜ Not started | none |
| 03 | [Secret-safety assertions](03-secret-safety-assertions.md) | ⬜ Not started | none |
| 04 | [Opt-in Windows integration tests](04-opt-in-windows-integration-tests.md) | ⬜ Not started | none |

## Key constraints

- Unit tests use fake adapters exclusively - never write real user
  Credential Manager entries or leave real DPAPI ciphertext on disk during
  unit test runs (plan.md Backend semantics: `wincred`).
- Integration tests that touch real DPAPI or Credential Manager APIs must be
  opt-in and clearly marked as Windows-only (plan.md Tests).
- No test output (assertion failure messages, logs) may contain secret
  values (plan.md Tests, C3).
