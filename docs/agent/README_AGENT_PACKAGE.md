# Agent Package README

This directory contains supporting technical documents intended to make autonomous implementation of the low-latency URL shortener significantly more reliable.

## Contents

### Existing project documents included

- `README.md`
- `docs/architecture/CURRENT_ARCHITECTURE.md`
- `docs/stages/01-STAGE_URL_SHORTENER_CORE.md`
- `docs/stages/02-STAGE_LINK_MANAGEMENT_FEATURES.md`
- `docs/stages/03-STAGE_BACKEND_ABSTRACTION.md`
- `docs/stages/04-STAGE_ANALYTICAL_EVENT_PIPELINE.md`
- `docs/stages/05-STAGE_OBSERVABILITY_HARDENING.md`

### Newly generated implementation-governance documents

#### Architecture

- `docs/architecture/IMPLEMENTATION_ARCHITECTURE.md`
- `docs/architecture/BACKEND_TOPOLOGY.md`
- `docs/architecture/PERFORMANCE_CONTRACT.md`
- `docs/architecture/SECURITY_MODEL.md`

#### Agent guidance

- `docs/agent/CODING_GUARDRAILS.md`
- `docs/agent/TESTING_REQUIREMENTS.md`
- `docs/agent/CI_PIPELINE.md`
- `docs/agent/DEVELOPMENT_WORKFLOW.md`
- `docs/agent/IMPLEMENTATION_TASKMAP.md`

## Suggested usage with Codex

1. Provide the stage specs and architecture docs together, not separately.
2. Treat guardrails and testing requirements as mandatory, not advisory.
3. Ask the agent to implement one stage or one major subtask at a time.
4. Require tests and docs in the same change set.
5. Use the task map as sequencing guidance, not as a replacement for the stage specs.
