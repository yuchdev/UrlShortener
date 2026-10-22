# 04 - Adding-a-transport guide verification

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 03; Task 05.0 (authors the guide)
**Blocks:** 13
**Estimated size:** S
**Suggested agent:** docs-updater (verification), cpp-expert (only to confirm the sketch compiles, in a scratch file outside the repo)

## Objective

Own the boundary with Task 05.0: 05.0 **authors**
`docs/commands/adding-a-transport.md` (IPC worked example, `ITransportAdapter`
contract); 08.0 **verifies, links and registers** it so the guide is
discoverable and true. This avoids two agents writing the same file.

## Dependencies

- Task 05.0's adding-a-transport-guide subtask deliverable (it writes the guide with the NDJSON framing table and shutdown table taken from the shipped stdio adapter; its banner says "Proposed" until ADR 0002 is accepted - flip it to match subtask 01): `docs/commands/adding-a-transport.md`, `include/url_shortener/commands/transport_adapter.hpp`, `src/transport/stdio_adapter.cpp`, `src/composition/` registration.
- 07.0 conformance harness (a new transport must pass the corpus: guide must say how, with the shim steps from 07.0 subtasks 03/05).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/commands/adding-a-transport.md` | modify (verification fixes only) | correct drift against the real header; add the "pass the conformance corpus" section if missing |
| `docs/commands/README.md` | modify | section 6 link (05.0 adds the sentence; verify) |
| `docs/adr/0002-unified-command-model.md` | modify | `## Links` entry (done in 01; verify) |

## Classes and functions (verification checklist)

The guide must contain, with these facts checked line by line:

1. The `ITransportAdapter` interface exactly as in `transport_adapter.hpp` (method names, signatures; ADR 0002 sketch: `name()`, `run(CommandRegistry&)`).
2. The three responsibilities (frame in, call `CommandRegistry::execute`, frame out) and the "no business rules, no per-command code" rule; caller identity derivation table (`CallerContext{principal, ControlSet, transport}`).
3. Registration point in `src/composition/` (file name and function) and the CLI subcommand wiring in `main.cpp` (file/lines described by symbol, not line number).
4. Walk-through using stdio as the reference implementation (file `src/transport/stdio_adapter.cpp`) and the IPC sketch (Unix domain socket / Windows named pipe, NDJSON framing, peer-credential identity, auth broker local-only rule from CLAUDE.md: never `0.0.0.0`).
5. A section "Pass the conformance corpus": add an `IConformanceAdapter` (C++) or `Adapter` (Python) shim, add the transport id to the lint list and `TRANSPORT_CAPS`, run `ctest -L conformance`, update the coverage matrix.
6. Security checklist: no listener unless explicitly designed, framing size limits, malformed frame behavior, audit transport tag, `--read-only` equivalent.
7. Links back to ADR 0002 and the catalog.

## Implementation steps

1. Read the guide next to the header and sources; fix discrepancies in the guide (never in code).
2. If the guide's IPC sketch contains code, compile-check it in a scratch file against the real header (outside the repo) and record "compile-checked on <date>" in the PR.
3. Add missing section (5) and (6) if 05.0 did not.
4. `/link-check docs/commands`.

## Commands

```bash
git grep -n "ITransportAdapter" -- include src docs
```

## Tests

None executable. Optional scratch compile check (not committed).

## Constraints

- Do not change the design described by 05.0; discrepancies in design go back to 05.0.
- No new file is created by this subtask.

## Success criteria

1. Every identifier in the guide exists in the source (`git grep` each).
2. The guide has sections 1-7 above.
3. Inbound links exist from the catalog, ADR 0002 and `docs/README.md` (the registry row is added in subtask 13).

## Out of scope / risks

- Implementing IPC (non-goal). Risk: if 05.0 did not produce the file, this subtask is blocked, not expanded.
