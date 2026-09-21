# Task 01.0 - CLI backend flag parsing & validation

**Parent milestone:** [Milestone 0005 - Windows Backend Selection](/docs/roadmap/0005-windows-backend-selection/plan.md)
**Status:** ⬜ Not started

## Scope

Introduce the `WindowsSecretBackend` enum and its parser, wire `--backend`
into `secret-store.exe`'s CLI argument handling, and validate the requested
value against both the set of known values and the current platform. This
task produces no adapters and does not touch secret material - it only
establishes the typed, validated value that later tasks construct an adapter
from.

This is the foundation every other task in this milestone depends on:
02.0/03.0/04.0 implement adapters against the enum this task defines, and
05.0 wires those adapters behind the parser this task builds.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Windows secret backend enum and parser](01-windows-secret-backend-enum-and-parser.md) | ⬜ Not started | 02, 03 |
| 02 | [Backend flag CLI wiring and error messages](02-backend-flag-cli-wiring-and-error-messages.md) | ⬜ Not started | none |
| 03 | [Platform validation: Linux and non-Windows rejection](03-platform-validation-linux-and-non-windows-rejection.md) | ⬜ Not started | none |

## Key constraints

- Keep CLI parsing separate from backend construction (plan.md Architecture)
  so parsing, validation, and factory selection can be tested independently.
- Unsupported backend values are fatal - never fall back to auto-selection
  (plan.md C2).
- Never log secret values while parsing/validating (plan.md C3 - not
  applicable to parsing itself, since no secret material exists yet, but
  error paths must stay clean of any future accidental payload echoing).
- C++17 only, consistent with the rest of the codebase.
