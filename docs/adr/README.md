# Architecture Decision Records

ADRs for Url Shortener use the [MADR](https://adr.github.io/madr/) (Markdown Any Decision Records) template.
Each record lives in this directory as `000N-slug.md`. Mermaid diagrams referenced by ADRs
are in `assets/`.

## Inventory

| ADR | Title | Status | Date |
|-----|-------|--------|------|
| [0001](0001-cli-rest-shared-command-layer.md) | CLI/REST Shared Command Layer | Accepted | 2026-09-23 |
| [0002](0002-unified-command-model.md) | Unified Command Model (JSON commands and jobs, pluggable transports) | Proposed | 2026-10-19 |
| [0003](0003-tui-framework-and-architecture.md) | TUI Framework (FTXUI) and Architecture | Proposed | 2026-10-19 |

## Template

Use `template.md` when creating a new ADR:

```bash
cp docs/adr/template.md docs/adr/000N-short-title.md
```

Replace the template placeholders with the record's number, title, date, and status.

## Naming conventions

- Filename: `000N-kebab-slug.md` - sequential, zero-padded to four digits.
- Status values: `Proposed` | `Accepted` | `Implemented` | `Superseded` | `Deprecated`.
- Superseded ADRs keep their file; add a `Superseded by: [000N](...)` line to their header.
