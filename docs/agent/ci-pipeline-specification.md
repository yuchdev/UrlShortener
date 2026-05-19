# CI Pipeline Specification

## 1. Purpose

This document defines the expected continuous integration behavior for the low-latency URL shortener project.
It is intended to keep autonomous implementation disciplined and to ensure each stage remains verifiable.

CI is not just a convenience layer; it is the enforcement mechanism for buildability, testability, and baseline quality.

---

## 2. CI objectives

The CI pipeline must:

1. verify the project builds from clean checkout
2. run automated tests consistently
3. catch contract regressions early
4. keep documentation and configuration changes visible in review
5. provide a stable path for backend-specific validation jobs

---

## 3. Minimum pipeline stages

The baseline CI workflow should include these stages/jobs.

### 3.1 Build job

Build the project in a clean environment.
Recommended matrix:

- Linux + GCC
- Linux + Clang

This matrix may be expanded later, but it should not be omitted from the baseline plan without reason.

### 3.2 Unit/service test job

Run fast tests that do not require heavyweight external services.
This job should be the main fast-feedback gate.

### 3.3 Integration test job

Run HTTP and backend integration tests.
This may be split into:

- no-external-dependency integration suite
- service-backed integration suite using PostgreSQL/Redis containers where configured

### 3.4 Static quality job

When tools are configured, run:

- formatting check
- lint/static analysis

This job should fail on violations rather than merely emit advisory output once the rules are adopted.

### 3.5 Documentation/configuration check job

At minimum, ensure documentation/config files expected by the project are present and referenced correctly.
If doc linting exists, integrate it here.

---

## 4. Optional but recommended jobs

### 4.1 Performance smoke job

A lightweight performance smoke test can detect severe regressions.
This should not attempt to be a full benchmark lab in CI.
Its role is to catch obvious accidental slowdown.

### 4.2 Migration validation job

For SQL-backed stages, validate that migrations apply cleanly from a fresh database and, where fixtures exist, from representative older schemas.

### 4.3 Packaging job

If the repository later produces release artifacts, a dedicated packaging job should verify packaging without mixing it into the core test gate.

---

## 5. Backend service provisioning in CI

When backend integration tests are enabled, CI should provision only the minimum required services, such as:

- PostgreSQL
- Redis

Provisioning may use containers or workflow service definitions.
The test job must clearly separate failures caused by code from failures caused by infrastructure startup.

---

## 6. Failure policy

A PR should not be considered merge-ready if any required CI job fails.
Temporary skips are allowed only when:

- the skip is documented,
- the reason is explicit,
- the affected scope is narrow,
- a follow-up issue/task exists.

“Will fix later” is not a sufficient CI bypass policy.

---

## 7. Recommended job order

A sensible order is:

1. build
2. fast unit/service tests
3. integration tests
4. static quality checks
5. optional perf/migration/packaging jobs

This ordering keeps quick failures visible early while still allowing deeper validation.

---

## 8. Artifact retention

CI should retain useful artifacts from failing or release-oriented runs where practical, such as:

- test reports
- coverage summaries
- benchmark summaries
- logs from integration test runs

Artifacts are particularly helpful for autonomous agent review loops.

---

## 9. Documentation in CI

CI configuration must be documented in repository docs sufficiently for a human or agent to understand:

- what jobs exist
- which jobs are mandatory
- which jobs require external services
- how to reproduce key jobs locally

---

## 10. Stage-aligned CI evolution

### Stage 01

CI must build and run core tests.

### Stage 02

CI expands to lifecycle/preview/stats tests.

### Stage 03

CI adds backend contract/migration/integration coverage.

### Stage 04

CI adds analytics pipeline tests and optional perf-smoke comparison.

### Stage 05

CI adds hardening/negative/fuzz-oriented coverage where practical and finalizes documentation checks.

---

## 11. Agent completion expectations

Autonomous implementation should treat CI green status as necessary but not sufficient.
A task is complete only when:

- required CI jobs pass
- tests added are meaningful
- docs are updated
- no known guardrail violation is left unresolved
