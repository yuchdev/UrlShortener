# Branching Policy

## 1. Purpose

This repository went through a period where branch names encoded the same
lifecycle stage in three different, inconsistent ways (`roadmap/`, `wip/`,
`milestone/`), and several branches outlived the PR that merged them —
leaving stale `.gitignore` drafts, duplicate "sync with master" commits, and
tracked log files scattered across branches that nobody cleaned up. This
policy exists to prevent that from recurring.

The core rule: a branch name's `{NNNN}` always matches a
`docs/roadmap/{NNNN}-{milestone-slug}/` folder (see
[docs/roadmap/README.md](/docs/roadmap/README.md) for that numbering). One
numbering scheme, not two.

---

## 2. Prefixes

| Prefix | Used for | Lifetime |
|---|---|---|
| `spec/{NNNN}-{slug}` | Writing or revising a milestone's roadmap docs, ADRs, and task/subtask specs — before any implementation code exists | Deleted the moment the spec PR merges |
| `feature/{NNNN}-{slug}` | Implementing a milestone's code. One branch per milestone, matching its `docs/roadmap/{NNNN}-{slug}/` folder exactly | Deleted the moment the implementation PR merges |
| `fix/{short-slug}` | Anything not tied to a milestone — bug fixes, chores, repo housekeeping | Deleted on merge |
| `task/{NNNN}-{TT.t}-{slug}` | Optional. Only when a milestone is large enough that a single `feature/` branch becomes unwieldy — one sub-branch per task, PR'd into the milestone's `feature/` branch, not into `master` | Deleted on merge into the parent `feature/` branch |

Do not invent a new prefix for a concept already covered above. If a branch
doesn't fit any row, it's probably `fix/`.

Bot-owned branches (`copilot/*` and similar agent-generated branches) are
outside this policy's naming rules, since the tool controls them — but they
are still subject to the cleanup rule in Section 3.

---

## 3. Cleanup

1. **Delete on merge, every time — locally and on the remote**
   (`git branch -d <name>` and `git push origin --delete <name>`).
2. Confirm the repository's "Automatically delete head branches" setting is
   enabled on GitHub, and don't push new commits to a branch after its PR has
   merged — doing so can detach it from that automatic cleanup and leave it
   orphaned.
3. Review bot-owned branches (`copilot/*`) promptly once the agent's work is
   merged or abandoned. Don't let them accumulate — leftover placeholder
   branches with no real content are pure noise in `git branch -r`.

---

## 4. Keeping a long-lived branch in sync

Never merge `master` back into a live `feature/` or `spec/` branch to "keep
it in sync" — this produces throwaway `Merge branch 'master' of ...` commits
that pollute history and make ancestry checks (`git merge-base
--is-ancestor`) harder to reason about.

Prefer:

- Keeping milestone branches short-lived, so sync is rarely needed.
- Rebasing onto `master` when a sync is unavoidable, rather than merging.
- Re-cutting the branch from current `master` if it has drifted far enough
  that a rebase would be painful.

---

## 5. Verifying a branch is safe to delete

Before deleting any branch, confirm its work actually reached `master`:

```bash
git merge-base --is-ancestor origin/<branch> master && echo merged || echo NOT merged
```

If this reports "NOT merged," diff the branch against `master`
(`git diff master origin/<branch>`) and account for every file before
deleting — don't assume a branch is disposable just because its PR shows as
closed or its name suggests old work.
