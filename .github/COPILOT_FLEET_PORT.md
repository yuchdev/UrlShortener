# Copilot-Native Fleet Port

This document records the port of the URL Shortener agentic fleet from the
**Claude Code** on-disk convention (`.claude/`) to the **GitHub Copilot CLI**
native convention (`.github/`).

It is a straight **mirror**: no agent/skill/hook behaviour was added, removed, or
redesigned. Only the on-disk **format and location** were translated so Copilot
CLI discovers and runs the fleet natively. The Python→C++ domain migration is
already documented in [`.claude/MIGRATION_REPORT.md`](../.claude/MIGRATION_REPORT.md);
this report only covers the Claude→Copilot format translation.

## Why the port was needed

Copilot CLI understands the *concepts* of agents, skills, and hooks, but it does
**not** auto-load Claude Code's files:

- It does **not** read `.claude/agents/*.md`, `.claude/skills/*/SKILL.md`, or the
  `.claude/settings.json` hook block.
- It only reads `CLAUDE.md` as an instruction file, not the rest of `.claude/`.
- Its hook protocol (stdin JSON shape and stdout decision fields) differs from
  Claude Code's `exit 2` convention.

The `.claude/` tree is retained as the source of truth; `.github/` is the
Copilot-native mirror generated from it.

## Discovery locations (Copilot CLI native)

| Kind   | Copilot CLI path                          | User-level fallback              |
|--------|-------------------------------------------|----------------------------------|
| Agents | `.github/agents/<name>.agent.md`          | `~/.copilot/agents/`             |
| Skills | `.github/skills/<name>/SKILL.md`          | `~/.copilot/skills/`             |
| Hooks  | `.github/hooks/*.json` (+ helper scripts) | inline `hooks` in `settings.json`|

Verified against the installed CLI package (`copilot 1.0.70`,
`…/pkg/win32-x64/1.0.70/`): builtin `definitions/*.agent.yaml`, the
`builtin-skills/*/SKILL.md`, and the hook config parser/schema in `app.js`.

## Agents — 11 ported

`.claude/agents/<name>.md` → `.github/agents/<name>.agent.md`

Frontmatter translation (body kept byte-identical apart from path rewrites):

| Claude frontmatter        | Copilot frontmatter          | Rule                                                        |
|---------------------------|------------------------------|-------------------------------------------------------------|
| `name`                    | `name`                       | unchanged                                                   |
| `description`             | `description`                | unchanged                                                   |
| *(none)*                  | `displayName`                | **added** (human-readable label)                            |
| `model: claude-opus-4-8`  | `model: claude-opus-4.8`     | dashes → dots in the version                                |
| `model: claude-sonnet-4-6`| `model: claude-sonnet-4.6`   | dashes → dots in the version                                |
| `tools: Read, Grep, …`    | `tools: view, grep, …`       | tool-name map below                                         |
| `allowed-tools: …`        | *(removed)*                  | Claude-only key; Copilot uses the single `tools` list       |

Tool-name map (Claude → Copilot):

| Claude      | Copilot        | Claude     | Copilot      |
|-------------|----------------|------------|--------------|
| `Read`      | `view`         | `WebFetch` | `web_fetch`  |
| `Grep`      | `grep`         | `WebSearch`| `web_search` |
| `Glob`      | `glob`         | `Agent`    | `task`       |
| `Edit`/`MultiEdit` | `edit`  | `Bash`     | `bash` (+ `powershell` added, Windows host) |
| `Write`     | `create`       | `TodoWrite`| *(dropped — Copilot has no equivalent tool; it uses native session todos)* |

Agents ported: `agent-orchestrator`, `app-architect`, `background-reviewer`,
`cpp-expert`, `docs-updater`, `docs-writer`, `feature-reviewer`,
`security-auditor`, `subtask-verifier`, `test-documenter`, `testing-expert`.
(`incident-analyst` from the reference project was already dropped in the C++
migration and has no source file.)

## Skills — 9 ported (+ 4 reference docs)

`.claude/skills/<name>/SKILL.md` → `.github/skills/<name>/SKILL.md`

| Claude frontmatter          | Copilot frontmatter    | Rule                                              |
|-----------------------------|------------------------|---------------------------------------------------|
| `name`, `description`       | `name`, `description`  | unchanged (`.claude/` refs in text → `.github/`)  |
| `allowed-tools: …`          | *(removed)*            | Claude-only key                                   |
| `invocation: /name <args>`  | *(removed)*            | Claude-only key                                   |
| *(none)*                    | `user-invocable: true` | **added** (Copilot marks user-callable skills)    |

`references/` subdirectories are copied verbatim (with `.claude/`→`.github/`
path rewrites). Skills ported: `adr-write`, `doc-registry`, `doc-xref`,
`document-tests`, `link-check`, `pr-review`, `secret-scan`, `test-gap`,
`verify-subtask`.

## Hooks — 8 behaviours ported

Source: `.claude/settings.json` + `.claude/hooks/*.py`
Target: `.github/hooks/fleet.json` + `.github/hooks/scripts/*.py`

### Config shape

Claude Code nests `{ "EventName": [ { "matcher", "hooks": [ {type,command} ] } ] }`.
Copilot CLI uses a top-level config object `{ "version", "hooks": { … } }` whose
events map **directly** to an array of hook specs:

```json
{ "version": 1, "hooks": { "preToolUse": [ { "type": "command", "matcher": "…", "command": "…" } ] } }
```

Verified: this exact structure loads without a schema error in the CLI (log:
`Loading repo hooks…`); the allowed spec keys are
`type, bash, powershell, command, exec, args, cwd, env, timeoutSec, timeout, matcher`.

### Event-name and matcher map

| Claude event / matcher              | Copilot event  | Copilot matcher (regex on tool name) | Script                              |
|-------------------------------------|----------------|--------------------------------------|-------------------------------------|
| `SessionStart`                      | `sessionStart` | —                                    | `session_start.py`                  |
| `PreToolUse` / `Write\|Edit\|MultiEdit` | `preToolUse` | `^(edit\|create)$`                 | `secret_scan.py`                    |
| `PreToolUse` / `Bash`               | `preToolUse`   | `^(bash\|powershell)$`               | `guard_bash.py`                     |
| `PostToolUse` / `Write\|Edit\|MultiEdit` | `postToolUse` | `^(edit\|create)$`               | `post_edit_format.py`, `doc_link_check.py` |
| `PostToolUse` / `mcp__github__.*`   | `postToolUse`  | `github-mcp-server`                  | `github_audit.py`                   |
| `Stop`                              | `agentStop`    | —                                    | `run_tests.py`, `doc_link_check.py` |

### I/O protocol translation

| Concern            | Claude Code                                  | Copilot CLI (this port)                                              |
|--------------------|----------------------------------------------|---------------------------------------------------------------------|
| Tool name / args   | `event.tool_name`, `event.tool_input`        | `event.toolCalls[0].toolName`, JSON in `…toolArgsJson`              |
| Block a tool call  | `exit 2` + stderr                            | stdout `{"permissionDecision":"deny","permissionDecisionReason":…}` |
| Block session stop | `exit 2` + stderr                            | stdout `{"decision":"block","reason":…}`                            |
| Inject context     | print to stdout                              | stdout `{"additionalContext":…}`                                    |
| Allow / no opinion | `exit 0`                                      | `exit 0`, no stdout                                                  |

`scripts/_common.py` centralises this and **tolerates both shapes**, so the
scripts also keep working under Claude Code. Behaviour, patterns, env vars
(`URLSHORTENER_SKIP_TESTS`, `URLSHORTENER_BUILD_DIR`, `URLSHORTENER_PROD_CONFIRMED`)
and log semantics are unchanged; logs now live under `.github/hooks/logs/`.

## Verification

- **Agents/skills**: frontmatter re-checked; bodies preserved (UTF-8 verified,
  e.g. `→` = U+2192); residual `.claude/` references rewritten to `.github/`.
- **Hooks**: all 8 scripts byte-compile; `fleet.json` is valid JSON in the
  `{version,hooks}` shape the CLI accepts. Functional tests with the real Copilot
  `toolCalls` stdin shape confirmed:
  - `secret_scan.py` denies a write containing a PEM private key; allows clean code.
  - `guard_bash.py` denies `rm -rf`; allows `git status`.
  - `session_start.py` emits `additionalContext` with live branch/commits.
  - `github_audit.py` logs the MCP call; `run_tests.py` honours the skip flag.

### Known limitation (hook runtime firing)

Copilot CLI **loads** repo hooks in headless one-shot (`-p`) prompt mode but does
**not execute** command hooks or extensions there (they load, then no-op) — hook
execution is gated to **interactive** trusted sessions (and, for prompt mode,
`GITHUB_COPILOT_PROMPT_MODE_REPO_HOOKS=1` / a trusted folder to load them). The
on-disk format is validated (config parses, scripts run correctly when fed the
Copilot event JSON directly), but end-to-end firing must be confirmed in a normal
interactive `copilot` session in this trusted repo.

## Relationship to `.claude/`

`.claude/` remains the source of truth and the Claude Code entrypoint. `.github/`
is the generated Copilot-native mirror. To regenerate agents/skills after editing
the source, re-run the transform saved with the session artifacts
(`port_fleet.py`); hook scripts were ported once and then adapted by hand for the
Copilot I/O protocol.
