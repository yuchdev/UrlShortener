# Agentic Fleet Migration Report — Aegis SWR (Python) → URL Shortener (C++)

This report documents the migration of the reference Python dev fleet (agents,
skills, and hooks from the "Aegis SWR" incident-forensics project) into a
C++-expert fleet for the **URL Shortener** — a C++17 low-latency service built on
Boost.Asio/Beast + OpenSSL, SOCI (SQLite/PostgreSQL), Redis/hiredis, and yaml-cpp,
built with CMake/vcpkg and tested with CTest/Boost.Test.

The migration was **not** a 1:1 copy. Every agent, skill, and hook had its
*specifics* changed from Python to C++ — build/test/lint/coverage tooling,
language patterns, and problem domain — while preserving the fleet's structure and
intent. This report records what was migrated, what was transformed, what was
dropped, and the external references that were removed (with follow-ups).

---

## 1. Destination layout

```
.claude/
  settings.json            # wires all 8 hooks to lifecycle events
  agents/    (10 agents)
  skills/    (9 skills, each a SKILL.md + optional references/)
  hooks/     (8 Python hook scripts + _common.py)
  logs/      (.gitignore -> runtime hook logs, not committed)
```

Convention: agent frontmatter uses `name/description/model/tools/allowed-tools`;
skills use `name/description/allowed-tools/invocation`. Models retained from
source: `claude-opus-4-8` (deep-reasoning roles) and `claude-sonnet-4-6`
(execution roles).

---

## 2. Agents migrated (10)

| Target agent | Source | Key transformation |
|---|---|---|
| `agent-orchestrator` | agent-orchestrator | C++ delegation flow; **incident-analyst branch removed** |
| `app-architect` | app-architect | ADR authority; C++ interface contracts; shortener domain |
| `cpp-expert` | **python-expert** | Full language transform: C++17 typing, RAII, clang-format/clang-tidy, ports-and-adapters, Result-style errors |
| `testing-expert` | testing-expert | Boost.Test/CTest, coverage via lcov `coverage` target, interface fakes |
| `feature-reviewer` | feature-reviewer | C++ correctness + shortener domain (lifecycle, SSRF, cache-aside) |
| `security-auditor` | security-auditor | STRIDE-lite centred on SSRF/TLS/SQLi/auth/secret-leak |
| `background-reviewer` | background-reviewer | vcpkg/FetchContent audit, clang-tidy, real secret-scan hook, perf |
| `docs-writer` | docs-writer | Doxygen; hand-written HTTP API tables (no runtime schema gen) |
| `docs-updater` | docs-updater | Doxygen sync; API-table refresh; xref |
| `subtask-verifier` | subtask-verifier | Spec compliance against `docs/specs/` + implementation-taskmap |
| `test-documenter` | test-documenter | Boost.Test classification (Unit/Fake/Integration/E2E), agent-driven (no codemod) |

**Dropped:** `incident-analyst`. It was referenced by the source orchestrator and
pr-review skill but **had no file in the source fleet** — it is an Aegis-only
incident-forensics role with no analogue in a URL shortener. All references to it
were removed (orchestrator delegation list, pr-review conditional fan-out branch).

---

## 3. Skills migrated (9)

| Target skill | Source | Key transformation |
|---|---|---|
| `adr-write` | adr-write | Inline MADR template (source read a `docs/adr/template.md` that does not exist here) |
| `verify-subtask` | verify-subtask | Points at `docs/specs/` not `docs/roadmap/`; escalates to `cpp-expert` |
| `test-gap` | test-gap | Coverage via lcov `coverage` target; shortener P0 risk rubric |
| `secret-scan` | secret-scan | Reuses the **real** `.claude/hooks/secret_scan.py`; C++ secret catalog |
| `link-check` | link-check | grep/glob-based (no `check_doc_links.py`); anchor-slug rules kept |
| `doc-registry` | doc-registry | `git ls-files`/`git grep`-based (no `doc_registry.py`) |
| `doc-xref` | doc-xref | `git grep`-based inbound search incl. Doxygen comments |
| `document-tests` | document-tests | Delegates to `test-documenter` agent (no `document_tests.py` codemod) |
| `pr-review` | pr-review | Two-agent fan-out; **incident-analyst branch dropped** |

References ported per skill: `adr-write/references/template-guide.md`,
`secret-scan/references/pattern-catalog.md`,
`test-gap/references/risk-ranking-rubric.md`,
`link-check/references/anchor-slug-rules.md` — each rewritten for C++/shortener
specifics.

---

## 4. Hooks migrated (8 + shared helper)

| Target hook | Source | Event | Key transformation |
|---|---|---|---|
| `_common.py` | _common.py | (shared) | Rebranded; `datetime.UTC` → `timezone.utc` for broader Python compat |
| `post_edit_format.py` | post_edit_format.py | PostToolUse Write/Edit | Runs **clang-format** on C/C++ files instead of ruff |
| `run_tests.py` | run_tests.py | Stop | clang-format + cmake build + `ctest -L unit` instead of ruff+pytest |
| `secret_scan.py` | secret_scan.py | PreToolUse Write/Edit | C++/shortener secret patterns; blocks on hit |
| `guard_bash.py` | guard_bash.py | PreToolUse Bash | Destructive + prod guard incl. PowerShell verbs |
| `doc_link_check.py` | doc_link_check.py | PostToolUse + Stop | Rebranded; skips gracefully when checker script absent |
| `github_audit.py` | github_audit.py | PostToolUse mcp__github__* | Project-neutral audit trail |
| `session_start.py` | session_start.py | SessionStart | Shortener reminders (fast path/SSRF/secrets/TLS); delegate list w/ cpp-expert |

Environment toggles: `URLSHORTENER_SKIP_TESTS`, `URLSHORTENER_BUILD_DIR`,
`URLSHORTENER_PROD_CONFIRMED`. Hooks verified: all 8 byte-compile; secret_scan and
guard_bash confirmed to block (exit 2) on TLS keys / Postgres DSNs / `rm -rf`;
placeholders allowed (exit 0); session_start emits live git context.

Per the user instruction, agents/skills now reference these **real** hook scripts
(not skills) as backstops — e.g. `cpp-expert`/`testing-expert` cite the Stop hook
`run_tests.py` and the `secret_scan.py` pre-hook; `background-reviewer` and the
`secret-scan` skill invoke `.claude/hooks/secret_scan.py` directly.

---

## 5. Language / tooling swaps (applied throughout)

| Python (source) | C++ (target) |
|---|---|
| `uv run pytest` | `ctest --test-dir <build> -C Debug` (Boost.Test) |
| ruff / ruff.toml | clang-format (`.clang-format`) + clang-tidy (`.clang-tidy`) |
| pytest-cov `--cov` | `coverage` CMake target (lcov/genhtml, `cmake/coverage.cmake`) |
| pyproject/uv.lock/pip-audit | `vcpkg.json` manifest + CMake `FetchContent` pins |
| `Optional[T]`, pydantic | `std::optional<T>`, structs/interfaces, const-correctness, RAII |
| reST docstrings `:param:` | Doxygen `/** @brief @param @return */` |
| FastAPI/Typer | Boost.Beast handlers / `boost::program_options` |
| asyncio | Boost.Asio `io_context` async |
| `# noqa` | `// NOLINT` |

## 6. Domain swap (incident forensics → URL shortening)

- Incident lifecycle / `RCAReport` / `SkepticVerdict` / consensus / HITL → **link
  lifecycle** (`active`/`disabled`/`expired`/`deleted`, precedence
  `deleted > disabled > expired > active`), redirect resolution, cache-aside,
  best-effort analytics.
- Hostile crash dumps/logs → untrusted user **URLs/slugs/HTTP bodies**, with
  **SSRF via private redirect targets** (`isPrivateHost`,
  `shortener_allow_private_targets`) as the primary threat.
- Factory-Strategy AI/debug backends → `StorageFactory`
  (inmemory/sqlite/postgres/redis).
- Secret surface: TLS PEM keys, Postgres/Redis DSNs, analytics HMAC salt, tokens
  — **removed** AWS/Anthropic/OpenAI/Google/Slack patterns (service integrates
  none).

---

## 7. External references removed (out of scope to rewrite)

Grouped by category. These were deleted from the migrated fleet rather than
rewritten, per the task scope.

### 7.1 Aegis domain concepts (deleted)
`RCAReport`, `SkepticVerdict`, `Hypothesis`, `HITLRequest`, `IncidentBrief`,
`InvestigationState`, consensus/convergence rounds, skeptic node, escalation/HITL
paths, incident de-duplication (`incident_similarity`), crash-dump/core/stack
parsing, `data_adapter.py`, debug/log backends, `fault_codes`.

### 7.2 Python tooling references (replaced, see §5)
uv, pytest, pytest-cov, ruff, pydantic, FastAPI, Typer, asyncio, pip-audit,
`pyproject.toml`, `uv.lock`, `slowapi`, `core/tokens.py` token budgeting.

### 7.3 Removed secret patterns
AWS access key id / secret access key, Anthropic, OpenAI, Google API keys, Slack
tokens — none apply to this service.

### 7.4 Referenced-but-absent helper scripts (source assumed; not present here)
The source fleet leaned on Python helper scripts that **do not exist** in this
repo. Skills were rewritten to run grep/glob/git-grep/ctest directly instead of
invoking fictional scripts:

- `scripts/check_doc_links.py` — doc link/anchor checker
- `scripts/doc_registry.py` — corpus registry builder
- `scripts/linkify_doc_mentions.py` — prose-mention linkifier
- `scripts/document_tests.py` — AST codemod for test docstrings
- `scripts/replace_optional.py` — Optional-rewrite codemod

The `doc_link_check.py` hook now guards for the missing `check_doc_links.py` and
skips cleanly rather than failing.

### 7.5 Referenced-but-absent docs / config
- `docs/dev/python_coding_standard.md` → replaced by `docs/agent/coding-guardrails.md`
  and `docs/agent/testing-requirements.md`
- `docs/adr/template.md` → replaced by an inline MADR template in `adr-write`
- `docs/README.md` index → referenced by several skills; does not exist yet
- `docs/roadmap/NNNN-.../` task specs → replaced by `docs/specs/` +
  `docs/agent/implementation-taskmap.md`
- `.mcp.json` (env-var expansion for secrets) → referenced by source; not present
- `docs/runbooks/`, `docs/post-mortems/`, `docs/reviews/`, `docs/security/` →
  agents will create these on first use

---

## 8. Follow-ups (recommended, out of current scope)

1. **Port doc-tooling scripts to this repo** so the automated gates match the
   skills: `scripts/check_doc_links.py` (unblocks the `doc_link_check.py` hook),
   `scripts/doc_registry.py`, `scripts/linkify_doc_mentions.py`.
2. **Build a C++ test-doc codemod** (libclang-based) to mechanise what the
   `document-tests` skill / `test-documenter` agent now do by hand.
3. **Create `docs/adr/template.md`** and a **`docs/README.md`** index so
   `adr-write`, `docs-writer`, and `docs-updater` can register/validate docs
   against real files.
4. **Decide on `.mcp.json`** (or equivalent) for GitHub/secret env-var expansion
   if MCP-based secret injection is desired.
5. **Populate `docs/specs/` stage specs** referenced by `subtask-verifier` /
   `verify-subtask` where gaps exist.

---

## 9. Verification performed

- All 8 hooks byte-compile (`python -m py_compile`).
- `settings.json` is valid JSON and wires every hook to the correct event.
- Functional hook tests: `secret_scan.py` blocks a TLS private key and a Postgres
  DSN (exit 2) and allows `${VAR}` placeholders (exit 0); `guard_bash.py` blocks
  `rm -rf /` (exit 2); `session_start.py` emits branch + recent commits + reminders.
- Fleet-wide grep confirms no stray `aegis`, `incident-analyst`, `python-expert`,
  `pytest`, `pydantic`, `fastapi`, or `docs/roadmap` references remain except
  intentional explanatory comparisons (the ruff→clang-format analogue comments and
  the documented incident-analyst drop note).
