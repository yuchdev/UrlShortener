# Testing Requirements Specification

## 1. Purpose

This document defines the minimum testing expectations for the low-latency URL shortener.
It applies to all stages and all contributors, including autonomous coding agents.

The system handles externally supplied input, lifecycle state, caching, backends, and asynchronous analytics. That combination requires a broad but disciplined testing strategy.

---

## 2. Testing goals

The test suite must provide confidence in:

1. domain correctness
2. HTTP/API contract stability
3. backend substitution correctness
4. redirect behavior under normal and degraded conditions
5. security-related validation behavior
6. bounded asynchronous analytics behavior
7. operational hardening and regressions

---

## 3. Required test classes

### 3.1 Unit tests

Unit tests must cover isolated logic such as:

- URL validation and normalization
- slug validation/generation rules
- lifecycle precedence (`deleted > disabled > expired > active` or the approved equivalent)
- redirect decision logic
- request/response model encoding where practical
- error mapping helpers
- cache record conversion helpers

### 3.2 Service tests

Service-level tests must cover orchestration without requiring full HTTP stack where practical.
Examples:

- create link success/failure paths
- conflict detection
- state transitions
- preview behavior
- stats service behavior
- redirect evaluation with stubbed repositories/cache

### 3.3 HTTP integration tests

HTTP tests must verify:

- route correctness
- method correctness
- JSON request/response envelopes
- status codes
- invalid payload handling
- redirect response headers/status
- request ID presence in error responses where required

### 3.4 Repository contract tests

Every metadata backend implementation must pass the same contract suite for supported behavior.
At minimum:

- create
- get by id
- get by slug
- update mutable fields
- enable/disable
- delete/restore semantics
- uniqueness conflict handling
- timestamp persistence where required

### 3.5 Cache behavior tests

Where cache is implemented, tests must cover:

- hit behavior
- miss behavior
- invalidation/update on write
- fallback when cache unavailable
- serialization/deserialization of cache records if applicable

### 3.6 Analytics pipeline tests

Analytics tests must cover:

- enqueue on redirect attempt/outcome
- bounded queue behavior
- drop behavior on overflow
- worker flush/persist behavior
- aggregation/query behavior
- failure handling without redirect failure

### 3.7 Performance and smoke benchmarks

A lightweight, automatable performance smoke layer should exist for regression detection.
It does not replace deeper benchmarking but should detect obvious regressions.

### 3.8 Hardening/fuzz/negative tests

Especially by Stage 05, tests must include malformed inputs and boundary cases such as:

- invalid JSON
- oversized body
- invalid slug/path shapes
- malformed URLs
- backend unavailability
- analytics queue saturation

---

## 4. Core behavior coverage matrix

The test suite must cover the following link states and outcomes consistently across service and/or integration levels:

- active link redirects successfully
- missing slug returns not found
- disabled link does not redirect
- expired link does not redirect
- soft-deleted link does not redirect
- restored link redirects again
- custom slug conflict returns conflict
- invalid target URL is rejected
- reserved slug is rejected

---

## 5. Backend-specific expectations

### 5.1 In-memory backend

Must be heavily used for unit/service tests because it enables deterministic fast feedback.
Thread safety behavior should be validated where concurrent access matters.

### 5.2 SQLite backend

Must have integration/contract tests covering:

- schema bootstrap/migration path
- basic CRUD semantics
- uniqueness constraints
- compatibility with documented local-dev assumptions

### 5.3 PostgreSQL backend

Must have integration/contract tests covering:

- migrations
- uniqueness enforcement
- transactional update behavior
- index-relevant lookup paths where practical

### 5.4 Redis cache

Must have integration tests or sufficiently realistic adapter tests covering:

- get/set/delete behavior
- invalidation/update semantics
- degradation when Redis unavailable

---

## 6. Test data and determinism rules

- Tests must not depend on wall clock timing beyond controlled abstractions where feasible.
- Prefer injectable clock/time source for expiry/lifecycle logic.
- Random slug generation tests should use deterministic seeds or stubs when asserting specific results.
- External service tests must avoid hidden dependency on developer-local state.

---

## 7. Concurrency and race-condition testing

At minimum, the suite should include targeted tests for:

- concurrent create with colliding custom slug
- concurrent generated slug create under repository uniqueness enforcement
- concurrent reads and updates in in-memory implementation where applicable
- analytics queue behavior under burst load

These do not need to prove absence of all races but must exercise known contention points.

---

## 8. CI expectations

The default CI pipeline should run:

- build
- unit tests
- integration tests that do not require heavy external infra, or run them in a dedicated job when infra is available
- lint/format/static analysis if configured

Backend-specific integration suites requiring PostgreSQL/Redis may run in separate CI jobs but must be part of the standard project validation story.

---

## 9. Coverage expectation

Absolute coverage percentage is not the only quality metric, but a practical target should be maintained.
Recommended baseline:

- meaningful overall line coverage target around **80%** for core/service code
- higher effective coverage for domain logic and lifecycle rules
- lower tolerance for untested redirect-state logic than for boilerplate startup code

Coverage reports must not become a substitute for scenario quality.

---

## 10. Regression testing rule

Every defect fixed after discovery should, where feasible, add:

- a direct regression test reproducing the issue, or
- a higher-level scenario test that would have caught it

---

## 11. Documentation-testing linkage

When documentation claims a behavior, there should be a test or benchmark path that verifies it where feasible.
Examples:

- route and status code contracts
- reserved slug rejection
- backend capability differences
- analytics best-effort semantics

---

## 12. Stage-specific testing additions

### Stage 01

- create/read/redirect basics
- invalid URL rejection
- redirect benchmark baseline

### Stage 02

- lifecycle operations
- preview
- provisional stats behavior
- reserved slugs

### Stage 03

- backend contract tests
- cache invalidation behavior
- migration tests

### Stage 04

- event queue behavior
- aggregation and retention hooks
- analytics enabled/disabled perf comparison

### Stage 05

- malformed input/fuzz cases
- logging/metrics/request-ID propagation checks where testable
- benchmark matrix/report validation

---

## 13. Completion rule

A stage or feature cannot be considered complete until the relevant automated tests are implemented, passing, and referenced from documentation where appropriate.
