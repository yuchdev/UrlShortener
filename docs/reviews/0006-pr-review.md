# PR review: milestone/0006-unified-command-model-and-tui (Task 07.0)

**Merge verdict: APPROVE**

- Range: `bf65f14~1..HEAD`, Task 07.0 "Tests and conformance" (subtask 34, step 8).
- Changed: 5 product files and about 115 test, tool and CI files.
  - Product: `src/core/utils.cpp` (the SSRF fix, rounds 1-3), plus `src/cli_parser.cpp` and `src/tui/tui_entry.cpp` with their headers (the `tui --path-root`/`--no-file-paths` flags).
- Reviewed by `feature-reviewer` and `security-auditor` in parallel. Both returned a verdict.
- This report brings the two verdicts together. Detailed security findings are in [0006-07.0-security-review.md](0006-07.0-security-review.md).

## Feature review (feature-reviewer)

**LGTM.** No blocking issues.

- The SSRF canonicalization is correct for every numeric IPv4 form, IPv6 classification (mapped, compatible, NAT64, 6to4, ULA, link-local) and special-use ranges. Public controls are not over-blocked.
- The tui path-flag wiring reuses `normalize_path_root`. Its mutual exclusion is checked at parse time, and it is role-agnostic: authorization stays in the registry.
- The Python and C++ harnesses are in lockstep. No `conformance` test runs under `unit`, and no `perf` test runs under `unit`.

## Security review (security-auditor)

**PASS** after the round 3 re-check. The initial verdict was PASS_WITH_FOLLOWUP.

- **Initial finding.** One MEDIUM SSRF bypass: the backslash or whitespace host terminator, as in `http://2130706433\x`. Two LOW findings had the same root cause.
  - Fixed in `0bfb543`: a strict host-charset allowlist, `is_valid_url_host`, and refusal of `\` before the path.
  - Vectors were added in `3236711`.
  - Re-check: the bypass class is closed and nothing remains open.
- **tui path flags.** Confinement is confirmed end to end:
  - a read-only session can never use paths;
  - `--no-file-paths` disables both directions;
  - the default is unchanged.
- **Supply chain.** These all pass:
  - a hermetic child environment;
  - list-argv spawns with timeouts and reaping;
  - loopback-only binding;
  - corpus data that cannot steer role, paths or backend.
- **CI.** Permissions are least privilege (`contents: read`). There is no `pull_request_target` and no untrusted `${{ }}` in `run:`, and artifacts contain no secrets.
- **Threat-brief coverage.** Every 07.0 threat has an executable check.

## Combined blocking items

None. The one MEDIUM finding was fixed and re-verified before this verdict.

## Non-blocking follow-ups

- **Security, LOW:** actions are pinned to mutable `@v4` tags, which matches the repository convention.
- **Security, LOW:** the host allowlist now refuses underscore hosts (`foo_bar.example.com`). This is fail-closed. A user decision is pending.
- **Security:** backport the SSRF fix, all three rounds, to `master`. A user decision is pending.
- **Feature:** `Errors:` lines for `link.delete`, `link.enable`, `link.disable` and `link.restore`. Done in `4bc641a`.
- **Feature:** `settle_until_idle` busy-yields. A 1 ms sleep would be gentler under CI load.
- **Feature:** two `sources.cmake` entries (`79_`, `80_`) are listed out of numeric order. This is cosmetic.
- **Feature:** the catalog says the `cmd` id default is `cli-1`, while the harness matches `cli-[0-9]+`. Both are accurate today.
