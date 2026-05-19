# Implementation Task Map for Coding Agents

## 1. Purpose

This document translates the stage specifications into an execution-oriented task map for autonomous development.
It does not replace the stage documents; it clarifies sequencing and expected deliverables.

---

## 2. Stage 01 task map

### 2.1 Domain and core types

Implement:

- `Link` domain model
- lifecycle fields required by Stage 01
- redirect type representation
- stable service/domain error model

Deliverables:

- core headers/sources
- unit tests for domain/state logic

### 2.2 Validation and slugging

Implement:

- URL validation/normalization service
- slug validation
- generated slug policy

Deliverables:

- service code
- tests for invalid scheme/host/slug cases

### 2.3 Repository and service

Implement:

- in-memory repository
- create/get-by-id/get-by-slug/update/delete primitives required by Stage 01
- service orchestration

Deliverables:

- repository implementation
- service tests

### 2.4 HTTP routes

Implement:

- create link endpoint
- read-by-id / read-by-slug endpoints per stage contract
- canonical redirect route
- stable error envelope

Deliverables:

- handler/router updates
- integration tests

### 2.5 Stage 01 docs and baseline perf

Deliverables:

- route/config docs updated
- benchmark baseline note/report format

---

## 3. Stage 02 task map

### 3.1 Lifecycle controls

Implement:

- enable/disable
- soft delete/restore
- expiry handling

### 3.2 Metadata and preview

Implement:

- tags/metadata/campaign fields within stage limits
- preview endpoint
- reserved slug protection

### 3.3 Provisional stats

Implement:

- basic aggregate counters/read endpoint
- clear provisional semantics in docs

### 3.4 Validation and docs

Deliverables:

- lifecycle tests
- preview/stats tests
- migration notes/docs updates

---

## 4. Stage 03 task map

### 4.1 Interface extraction

Implement:

- metadata repository interfaces
- cache interface
- backend-agnostic service wiring

### 4.2 Backend adapters

Implement:

- SQLite metadata backend
- PostgreSQL metadata backend
- Redis cache adapter
- null/in-memory test adapters as needed

### 4.3 Configuration and migrations

Implement:

- backend selection/config
- schema migrations/bootstrap
- cache invalidation strategy

### 4.4 Validation

Deliverables:

- repository contract tests
- backend integration tests
- migration tests
- backend topology docs updates

---

## 5. Stage 04 task map

### 5.1 Analytics model and queue

Implement:

- click event model
- bounded event queue
- best-effort enqueue from redirect flow

### 5.2 Worker and persistence

Implement:

- analytics worker
- repository persistence
- aggregation/read API support

### 5.3 Validation

Deliverables:

- overflow/backpressure tests
- aggregation tests
- perf comparison enabled vs disabled
- privacy/anonymization docs

---

## 6. Stage 05 task map

### 6.1 Observability

Implement:

- structured logging
- request/correlation ID propagation
- metrics counters/hooks
- health/readiness behavior

### 6.2 Hardening

Implement:

- malformed-input protections
- boundary checks
- negative tests
- operator runbook/documentation updates

### 6.3 Final verification

Deliverables:

- benchmark matrix/report format
- release/migration notes
- config and deployment docs

---

## 7. Cross-stage deliverable rule

Every stage/subtask must deliver all of:

- code
- tests
- documentation
- validation notes

A partially implemented stage without those four elements is incomplete.
