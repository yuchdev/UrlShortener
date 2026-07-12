---
name: adr-write
description: User-invoked as /adr-write <title>. Scaffolds a new Architecture Decision Record in docs/adr/ using the MADR template, pre-filling context from recent git log and open GitHub issues. Use when a design decision needs to be recorded before implementation.
allowed-tools: Read, Grep, Glob, Bash, Write, Edit
invocation: /adr-write <title>
---

# ADR Write (MADR)

Scaffold a new ADR for the decision titled `$ARGUMENTS`.

## Steps

1. Determine the next ADR number: list `docs/adr/`, find the highest `NNNN-`
   prefix, increment (zero-padded to 4 digits). If the directory or no ADR exists,
   create `docs/adr/` and start at `0001`. Filename:
   `docs/adr/NNNN-<kebab-title>.md`.
2. Seed **Context** from the repo: run `git log --oneline -10` and, if `gh` is
   authenticated, `gh issue list --state open --limit 10`. Summarise what is
   prompting this decision (e.g. a new storage backend, a redirect-policy change,
   a TLS/config decision).
3. Fill the MADR template below with `NNNN`, the title, today's date (from session
   context), and the seeded Context. Leave **Status: Proposed**. See
   [references/template-guide.md](references/template-guide.md) for what each
   section must contain and how the seeded git/issue context maps onto it.
4. Add an index line to the `docs/` index (`docs/README.md` - create it if absent).

> **Note:** the reference project read a canonical `docs/adr/template.md`. That
> file does not exist here yet, so this skill carries the MADR template inline
> below. Creating `docs/adr/template.md` is tracked as a follow-up in the
> migration report; once it exists, prefer reading it at generation time.

## Template (MADR, inline)

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

Print the path of the created ADR and a one-line summary. The `app-architect`
agent owns the decision content - this skill only scaffolds and seeds it. Leave it
`Proposed` for human/architect sign-off.

## Completion checklist

- [ ] File created at `docs/adr/NNNN-<kebab-title>.md` with correct zero-padded sequence number
- [ ] Structure matches the MADR template (Context, Decision, Alternatives Considered table, Consequences Positive/Negative, Validation / Rollout, Links) - no ad-hoc headings
- [ ] Status is `Proposed` - not Accepted (that requires human/architect sign-off)
- [ ] `Alternatives Considered` table has at least the chosen option + one rejected alternative with a stated reason
- [ ] `Links` section points at the driving spec (`docs/specs/…`) / issue used to seed Context
- [ ] The `docs/` index has a new line for the ADR
- [ ] The link-check skill passes on the new file
