# URL Shortener Roadmap

Planning and progress tracking for the URL Shortener project, organised as a
three-tier hierarchy: **Milestone → Task → Subtask**.

## Hierarchy & vocabulary

| Tier          | Meaning                                                                                                       | Lives in                                                |
|---------------|-----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------|
| **Milestone** | A large, strategic initiative / development direction (e.g. the REST API refactor).                            | A folder `docs/roadmap/{NNNN}-{milestone-slug}/`.       |
| **Task**      | One deliverable unit of a milestone (e.g. "Router Infrastructure"). Listed in the milestone's `## Tasks` table. | A subfolder `…/{TT.t}-{task-slug}/` with a `README.md`. |
| **Subtask**   | An atomic, implementable spec (one file, one focused change/test set). Listed in the task's `## Subtasks` table. | A file `…/{TT.t}-{task-slug}/{NN}-{subtask-slug}.md`.   |

A *subtask* is part of a *task*; a *task* is part of a *milestone*.

## File & folder convention

```
docs/roadmap/
  README.md                              ← this index + convention
  {NNNN}-{milestone-slug}/               ← MILESTONE
    plan.md                              ← milestone spec; opens with a `## Tasks` table
    status.md                            ← progress tracker; `## Current status` table
    {TT.t}-{task-slug}/                  ← TASK
      README.md                          ← task spec; opens with a `## Subtasks` table
      {NN}-{subtask-slug}.md             ← SUBTASK spec
```

- `{NNNN}` - zero-padded milestone number (`0001`, `0002`, …).
- `{TT.t}` - task number, carried from the milestone's `## Tasks` table (e.g. `01.0`, `02.0`, …).
- `{NN}` - zero-padded subtask order (`01`, `02`, …), local to its task folder.
- Slugs are kebab-case.

A milestone may also keep supplementary, cross-cutting design documents at its
own root (e.g. decision records, API contracts, data models) that several
tasks reference. These are not part of the Milestone → Task → Subtask chain
themselves - they are shared source material the `plan.md` and task
`README.md` files link out to, kept alongside rather than duplicated into
every task/subtask that needs them.

### Heading vocabulary

- A milestone's `plan.md` lists its tasks under a `## Tasks` heading (table column: `Task`).
- A task's `README.md` lists its subtasks under a `## Subtasks` heading.
- Status markers used throughout: `✅ Complete` · `🔶 In progress / partial` · `⬜ Not started`.

### Linking convention

When a document references another **specific** document, use an
absolute-from-repo-root Markdown link:

```
[docs/roadmap/0001-rest-api-refactoring/plan.md](/docs/roadmap/0001-rest-api-refactoring/plan.md)
```

- Always a leading `/` (repo root), never relative `../../` chains.
- **Template** paths (containing `{` / `}`, e.g. `` `{NN}-{subtask-slug}.md` ``) stay as
  backtick code spans, not links.

## Milestones

| #    | Milestone                                        | Spec                                                                | Status                                                                  |
|------|---------------------------------------------------|----------------------------------------------------------------------|---------------------------------------------------------------------------|
| 0001 | REST API refactoring (router, handlers, OpenAPI docs) | [plan.md](/docs/roadmap/0001-rest-api-refactoring/plan.md)           | [status.md](/docs/roadmap/0001-rest-api-refactoring/status.md)            |
| 0002 | Web admin console (analytics, fingerprinting, abuse detection) | [plan.md](/docs/roadmap/0002-admin_console/plan.md)                  | [status.md](/docs/roadmap/0002-admin_console/status.md)                   |
| 0003 | CLI & REST command interfaces (shared command layer for link management) | [plan.md](/docs/roadmap/0003-cli_rest_interfaces/plan.md) | [status.md](/docs/roadmap/0003-cli_rest_interfaces/status.md) |
| 0004 | Public web UI (paste a URL, get a short link, copy it) | [plan.md](/docs/roadmap/0004-web-ui/plan.md)                        | [status.md](/docs/roadmap/0004-web-ui/status.md)                          |
| 0005 | Windows secret-store backend selection             | [plan.md](/docs/roadmap/0005-windows-backend-selection/plan.md)      | [status.md](/docs/roadmap/0005-windows-backend-selection/status.md)       |

> All five milestones are decomposed into the full task → subtask tree.
> Milestone 0001 is **fully implemented** (see its
> [status.md](/docs/roadmap/0001-rest-api-refactoring/status.md) for verified
> test/build evidence). Milestones 0002, 0003, 0004, and 0005 are fully
> specified but **not started** - see each one's `status.md` for open risks,
> and note that 0005's plan.md carries an unresolved verification question (no
> existing Linux `secret-store` implementation could be located in this
> repository to confirm parity against).
