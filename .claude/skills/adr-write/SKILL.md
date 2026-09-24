---
name: adr-write
description: User-invoked as /adr-write <title>. Scaffolds a new Architecture Decision Record in docs/adr/ using the MADR template, pre-filling context from recent git log and open GitHub issues. Use when a design decision needs to be recorded before implementation.
allowed-tools: Read, Grep, Glob, Bash, Write, Edit
invocation: /adr-write <title>
---

# ADR Write (MADR)

Scaffold a new ADR for the decision titled `$ARGUMENTS`.

## Steps

1. Determine the next ADR number: list `docs/adr/`, find the highest
   `NNNN-` prefix, increment (zero-padded to 4 digits). If the directory or no
   ADR exists, create `docs/adr/` and start at `0001`. Filename:
   `docs/adr/NNNN-<kebab-title>.md`.
2. Seed **Context** from the repo: run `git log --oneline -10` and, if `gh` is
   authenticated, `gh issue list --state open --limit 10`. Summarise what is
   prompting this decision.
3. **Fill the inline MADR template below** with `NNNN`, the title, today's date
   (from session context), and the seeded Context. Leave **Status: Proposed**.
   See the section spec in [references/template-guide.md](references/template-guide.md)
   for what each section must contain and how the seeded git/issue context maps
   onto it. If this project already has accepted ADRs, read the most recent one
   as a filled exemplar of the house style.
4. Add an index line to `docs/README.md`.

## Template (MADR, inline)

> **Note:** the reference project read a canonical `docs/adr/template.md`. That
> file does not exist in this repo yet, so this skill carries the MADR template
> inline below. Creating `docs/adr/template.md` is a tracked follow-up; once it
> exists, prefer reading it at generation time so a single source of truth cannot
> drift.

```markdown
# NNNN. <Title>

- Status: Proposed
- Date: <YYYY-MM-DD>

## Context

<What decision is being forced, and why now. Seeded from git log + open issues.>

## Decision

<The choice made, in the active voice: "We will …">

## Alternatives Considered

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| <chosen> | … | … | Chosen |
| <rejected alt> | … | … | Rejected because … |

## Consequences

### Positive
- …

### Negative
- …

## Validation / Rollout

<How we will know this was right; migration/rollback plan; which tests or
benchmarks (e.g. redirect-latency, CTest labels) gate it.>

## Links

- Driving spec/issue: <docs/specs/… or #issue>
- Related ADRs: <NNNN>
```

## Output

Print the path of the created ADR and a one-line summary. Note that the
`app-architect` agent owns the decision content - this skill only scaffolds and
seeds it. Leave it `Proposed` for human/architect sign-off.

## Completion checklist

- [ ] File created at `docs/adr/NNNN-<kebab-title>.md` with correct zero-padded sequence number
- [ ] Structure matches the inline MADR template (Context, Decision, Alternatives Considered table, Consequences Positive/Negative, Validation / Rollout, Links) - no ad-hoc headings
- [ ] Status is `Proposed` - not Accepted (that requires human/architect sign-off)
- [ ] `Alternatives Considered` table has at least the chosen option + one rejected alternative with a stated reason
- [ ] `Links` section points at the driving roadmap task / issue used to seed Context
- [ ] `docs/README.md` has a new index line for the ADR
- [ ] `/link-check` passes on the new file
