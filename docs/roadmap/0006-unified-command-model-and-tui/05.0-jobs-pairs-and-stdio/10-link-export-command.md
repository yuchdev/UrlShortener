# 10 - link.export command

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/04 (transitively 01.0/06-07: `permission_for`), 05.0/07, 05.0/08 (shares `pair_commands.cpp`/`PairCommandDeps`), 03.0/01 (`ListLinksPage`), 03.0/12 (`ILinkLister`), 03.0/14 (`link.list` filter set)
**Blocks:** 20, 06.0/15 (Import/Export screen), 06.0/20 (manual export check)
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert; security review: security-auditor)

## Objective

Register `link.export` (catalog 3.3): a `kind=job`, `read` command that pages through the same filtered link query as `link.list`, serializes each link as CSV (`full_url,slug`) or NDJSON (a `link.create`-compatible args object) and delivers it either as inline `data` in the job result (size-capped) or to a local `path` written atomically. The output of an export must be accepted unchanged by `link.import`.

## Dependencies

- 05.0/04, 05.0/07 (`AtomicFileSink`, `kMaxInlineExportBytes`), 05.0/08 (`PairCommandDeps`, registration function).
- The list entry point shared with `link.list` is `app::ILinkLister` (03.0/12, over `ListLinksPage` of 03.0/01; filter set of `link.list`,
  03.0/14: `status` incl. `expired`, `q`, `tag`, `campaign`, `sort`, `order`, `offset`, `limit` 1..500, returning items plus `total`). This spec
  wraps it as `LinkPageSource`; `build_command_runtime` (05.0/20) binds it with `list_page_from(std::shared_ptr<ILinkLister>)`, capturing the
  `shared_ptr` by value so the job may outlive a per-request runtime. Export must NOT call `IMetadataRepository` directly: that would be a second query path next to `link.list`.
- Verified: `app::LinkView` fields (`slug`, `target_url`, `redirect_type`, `expires_at`, `enabled`, `tags`, `metadata`, `campaign`, `status`), `redirectTypeToString` in `core/utils.hpp`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/pair_commands.hpp` | modify | `LinkPageSource` alias, `PairCommandDeps::list_page` |
| `include/url_shortener/commands/pair_formatter.hpp` | create | `format_csv_row`, `format_ndjson_row`, `csv_header` |
| `src/commands/pair_formatter.cpp` | create | formatters (shared by tests with the 06 parser for round trip) |
| `src/commands/pair_commands.cpp` | modify | `link.export` descriptor, factory, body |
| `tests/unit/commands/59_link_export_command.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/pair_formatter.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
namespace url_shortener::commands {
struct LinkListFilter { std::string status = "active", q, tag, campaign, sort = "created_at", order = "desc"; };
struct LinkPage { std::vector<app::LinkView> items; std::size_t total = 0; };
using LinkPageSource = std::function<app::Result<LinkPage>(const LinkListFilter&,
                                                          std::size_t offset, std::size_t limit)>;
// PairCommandDeps gains:  LinkPageSource list_page;

inline constexpr std::size_t kExportPageSize = 200;

std::string csv_header();                                        // "full_url,slug\n"
std::string format_csv_row(const app::LinkView& link);           // quotes fields containing , " CR LF
std::string format_ndjson_row(const app::LinkView& link);        // one line + "\n", keys = link.create args
}
```

Descriptor `link.export`: base perm `read`, `permission_for = [](const ArgsView& a){ return a.has("path") ? Permission::write : Permission::read; }`
(decision log D16, evaluated by `execute` after `ArgSpec` validation, 01.0/07 step 4a; `command.list` reports the base permission
`read`, and the catalog 3.3 states the `path` rule in prose), kind job, not mutating, no dry-run. Args: `format` (enum, required), `status` (default `active`), `q`, `tag`, `campaign`, `path`. `status=all` allowed (TUI sends it). Unknown `sort` is not exposed (fixed `created_at desc, slug` for stable paging).

Factory (runs only after `execute` has applied `permission_for`, so a read-only caller (`ControlSet::User`, i.e. `--read-only` sessions and
the default REST role) with `path` has already received `not_authorized` and the factory is never invoked; the factory does **no**
permission check of its own): REST + `path` -> `unsupported`; `path` -> `AtomicFileSink::create_new(path,
deps.file_paths)` (path rules and root policy from 07, fails before queueing); no `path` -> inline sink.

Body: write `csv_header()` for CSV; `offset = 0`; loop `list_page(filter, offset, kExportPageSize)`; `ctx.set_total(total)` from the first page; per item check `ctx.cancel_requested()`, append the formatted row (inline: fail the job `invalid_field` "result too large; use path" once `kMaxInlineExportBytes` would be exceeded; path: `sink.write`), `ctx.advance(1)`; advance `offset` by the page size; stop on an empty page or `offset >= total`; list error -> job `failed` with the mapped code. End: file sink `commit()` and result `{"rows":n,"path":"...","data":null}`; inline `{"rows":n,"path":null,"data":"..."}` (escaped with `jsonString`); cancelled -> sink `abort()`, result `{"rows":n,"path":null,"data":null}`, outcome `cancelled`.

Paging under concurrent writes: offset paging may skip or repeat a row if links are created or deleted during the export. Documented as best effort (a snapshot export needs a repository cursor, out of scope).

NDJSON row keys: `target_url`, `slug`, `redirect_type`, `enabled`, `tags`, `metadata`, plus `expires_at` and `campaign` only when present; no `id`, `short_url`, timestamps or status (not valid `link.create` args and would be rejected on re-import).

## Implementation steps

1. Write `pair_formatter` with unit tests first; for CSV, quote a field when it contains `,`, `"`, `\r` or `\n`; double embedded quotes.
2. Add `LinkPageSource` to `PairCommandDeps` and keep default-constructed (empty) meaning "export unavailable" -> descriptor not registered (`register_pair_commands` registers `link.export` only when `list_page` is set).
3. Implement factory and body; reuse the `JobBody` structure from 08.
4. Round-trip test: build N links in the fake store, export CSV and NDJSON, import into an empty fake store with `link.import`, compare slug/target sets.
5. Verify `docs/commands/README.md` section 3.3 result shape matches.

## Commands

```text
cmake --build cmake-build --target commands__59_link_export_command
ctest --test-dir cmake-build -R "^commands__59_link_export_command$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/59_link_export_command.cpp` | `commands__59_link_export_command` | unit | CSV inline: header + rows, `rows` count; NDJSON inline keys exactly the `link.create` set; zero matching links -> header only (csv) / empty data (ndjson), `rows:0`; filters (`status`, `q`, `tag`, `campaign`) are passed through to the page source verbatim; pagination boundary: 0, 199, 200, 201, 401 links (fake source with page size recording) produce all rows exactly once; inline cap: output over `kMaxInlineExportBytes` -> job `failed`/`invalid_field` with no partial `data`; `path` export writes the file, `path` in result, no temp file left; existing `path` -> `invalid_field`; REST + `path` -> `unsupported`; cancel mid-export (gate on page 2): state `cancelled`, no file at the target path, no temp file; list failure -> job `failed` with mapped code; formatter: commas, quotes and newlines in `metadata`-free fields round trip through `PairLineParser`; round trip export -> import equality for both formats; `ControlSet::User` allowed for inline export (read) but `not_authorized` with `path` via `permission_for` (factory call count 0, no file created; audit has no `job_accepted`); `PathPolicy{allow_paths=false}` + `path` -> `unsupported`; `list_page` unset -> command absent from `command.list`; a slug and target never start with `=`, `+`, `-`, `@` (property check over the generator's alphabet and `isValidSlug`) |

Manual check: export 1 000 links to a file from the TUI/CLI, then import it into a fresh database and compare `link.list` totals.

## Constraints

- C1: one query path (`link.list`'s); no direct repository access from the export handler.
- C6: output contains only link fields; never `client_id_hash` or analytics data; no secrets.
- Path rules from 07 (no overwrite, UNC rejected, atomic commit, optional root). Writing a file is a side effect of a `read` command; allowed only for local transports with a write-capable caller (REST rejected, read-only sessions rejected).
- CSV formula injection: cells are not neutralized (that would break re-import) and do not need to be: targets are always `http(s)://...` and `isValidSlug` is `^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$` (verified, `src/core/utils.cpp`), so no cell can start with `=`, `+`, `-` or `@`. 07.0/20 asserts this.
- C9/C++17/naming; `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__59_link_export_command` passes.
2. Round-trip test (export -> import) yields identical slug/target sets for CSV and NDJSON.
3. Cancelling an export leaves no file and no `.tmp-*` residue (asserted).
4. `grep -n "IMetadataRepository" src/commands/pair_commands.cpp` returns nothing.

## Out of scope / risks

- Snapshot-consistent exports, compression, other formats, exporting `click_events`.
- Memory: inline mode keeps up to 1 MiB; file mode streams page by page.
