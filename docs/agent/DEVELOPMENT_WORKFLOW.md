# Development Workflow for Autonomous Implementation

## 1. Purpose

This document defines the expected workflow for implementing the low-latency URL shortener with coding agents and review loops.
It is intended to minimize architectural drift, reduce partially finished work, and keep each stage verifiable.

---

## 2. General workflow principle

Implementation should proceed **stage by stage**, not as a single undifferentiated burst of changes.
Each stage should end in a reviewable, testable, documented state.

Large cross-stage refactors are allowed only when they are clearly required to preserve architecture or correctness and are documented in the PR/implementation notes.

---

## 3. One-stage-at-a-time rule

Preferred execution model:

1. choose a single stage or clearly scoped subtask
2. review relevant specs and guardrails
3. implement code
4. add/update tests
5. update documentation
6. run validation/CI
7. prepare review notes

Avoid mixing unrelated stage work into one PR unless the repository owner explicitly requests it.

---

## 4. Recommended PR/task granularity

Preferred granularity is one of:

- one PR per stage, or
- one PR per major subtask within a stage if the stage is large

Examples of acceptable subtask splits:

- Stage 03 metadata repository interfaces + in-memory adapter
- Stage 03 SQLite/PostgreSQL adapters + contract tests
- Stage 04 queue/worker core + repository integration

Examples of poor splits:

- one PR mixing Stage 03 backends and Stage 05 observability hardening
- one PR introducing many routes, backends, and analytics without isolated validation

---

## 5. Pre-implementation checklist

Before writing code for a task, the agent should verify:

1. Which stage document governs the work?
2. Which architecture/guardrail docs apply?
3. Which module boundaries are affected?
4. What tests are required?
5. What docs/configs must be updated?
6. Does the change touch the redirect fast path?

---

## 6. Implementation checklist

During implementation, the agent should:

- preserve layer boundaries
- keep new interfaces minimal but sufficient
- avoid introducing dead abstractions “for future flexibility” without stage demand
- prefer deterministic behavior and explicit error handling
- preserve backward compatibility where the stage requires it

---

## 7. Validation checklist

Before considering a task complete, the agent should confirm:

- code builds cleanly
- relevant tests pass
- new tests exist for new behavior
- documentation reflects behavior
- configuration changes are documented
- no obvious performance/security regression was introduced

---

## 8. Review-note expectations

Each implementation unit should include a concise review note covering:

- scope implemented
- key files/modules changed
- tests added/updated
- documentation updated
- follow-up items intentionally deferred

This helps both humans and agents review efficiently.

---

## 9. Handling ambiguity

When specifications leave room for interpretation, the agent should prefer:

1. correctness
2. fast-path protection
3. consistency with existing stage docs
4. simplest implementation compatible with future stages

Do not resolve ambiguity by inventing expansive frameworks or speculative features.

---

## 10. Handling follow-up work

If a task uncovers missing prerequisites, the preferred behavior is:

- implement the minimum required prerequisite cleanly,
- document it in review notes,
- keep the rest of the task scoped.

Avoid sprawling opportunistic rewrites.

---

## 11. Completion criteria

A task/stage is complete only when all are true:

- required behavior is implemented
- tests are present and passing
- docs are updated
- CI expectations are met
- any known limitation is documented, not hidden

---

## 12. Suggested autonomous execution order

Recommended macro-order:

1. Stage 01 core
2. Stage 02 lifecycle/management
3. Stage 03 backend abstraction
4. Stage 04 analytics pipeline
5. Stage 05 observability/hardening

Within a stage, prefer implementing foundational interfaces/types before routes and adapters.

---

## 13. Do-not-do list

- do not mark a stage complete with TODO stubs replacing real logic
- do not skip tests because “manual verification succeeded”
- do not postpone documentation until the end of all stages
- do not collapse multiple concerns into one opaque mega-commit/PR
- do not ignore performance implications for redirect-path changes
