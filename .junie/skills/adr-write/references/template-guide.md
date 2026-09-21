# ADR template guide (section-by-section)

This guide explains what each section of the MADR template (carried inline in
`SKILL.md`) must contain and how the git/issue context the skill gathers in Step 2
maps onto it. When `docs/adr/template.md` is eventually created (a migration
follow-up), read it for the exact headings and read this for how to fill them.

## Filename & header

- Filename: `docs/adr/NNNN-<kebab-title>.md`, `NNNN` zero-padded, one higher than
  the current maximum in `docs/adr/`.
- H1: `# NNNN. Title`.
- Status starts **Proposed**. Only a human/architect flips it to `Accepted`.
  `Superseded` / `Deprecated` are set later, with a link to the ADR that replaces
  it.

## Context

*What forces this decision.* This is where the seeded material goes:

- `git log --oneline -10` → the recent commits that surfaced the problem.
- `gh issue list --state open --limit 10` → any open issue the decision resolves;
  link it in **Links**, don't just paraphrase it.
- State the constraints and forces plainly. No solution yet.

## Decision

The change being adopted, in the present tense ("We will …"). One clear choice.
If it is still genuinely open, the ADR is premature — write it once the
`app-architect` has a direction, since this skill only scaffolds.

## Alternatives Considered

A **table** (`| Option | Pros | Cons | Verdict |`). Include the chosen option's
serious rivals — an ADR with an empty or single-row table reads as undecided.
Every rejected row needs a concrete reason, not just "worse". For this project,
typical rivals are storage backends (sqlite vs postgres vs redis), cache
strategy (cache-aside vs write-through), or redirect status semantics (301 vs
302/307/308).

## Consequences

Split **Positive** and **Negative**. Negative is the honest part: migration cost,
new failure modes, lock-in. For a URL Shortener change, call out anything touching
the **redirect fast path** (added latency, extra I/O), **link lifecycle**
correctness, **cache invalidation**, or **security** (SSRF surface, auth, secret
handling) explicitly — those are the consequences a reviewer looks for first.

## Validation / Rollout

How we confirm the decision works and how we ship it. Name the follow-up work and
who owns it: implementation → `cpp-expert`, tests → `testing-expert`, doc/runbook
updates → `docs-updater`. Reference concrete gates: the `coverage` CMake target,
CTest labels (`unit`/`integration`/`contract`), and any redirect-latency
benchmark.

## Links

- **Driving spec/issue:** the `docs/specs/…` spec or GitHub issue that drove this.
- **Supporting specs / diagrams:** anything under `docs/specs/` or `docs/`.
- **Supersedes / Superseded by:** prior ADRs in the same decision lineage.

## Anti-patterns to avoid

- Marking a fresh ADR `Accepted` — sign-off is a human step.
- An empty Alternatives table or an all-positive Consequences section — both
  signal the decision wasn't really weighed.
- Recording an implementation detail as an ADR — ADRs capture *decisions with
  trade-offs*, not routine code.
