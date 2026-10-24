# 12 - Links list screen (D3) and link actions

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 06, 08, 09, 10, 11 (result viewer for preview); 03.0/14 (`link.list`), 03.0/11 (`link.*` commands registered)
**Blocks:** 06.0/13, 17, 18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Build the Links tab: **D3** filter bar (`filter: [camp:spring]`, status radio
`(all) active disabled expired deleted`), the table of slug / target URL / status /
clicks / expires, and the action keys `Enter detail`, `n new`, `e edit`,
`t toggle`, `d delete`, `u restore`, `p preview`, `s stats`. It is a bespoke
convenience layered on generic calls (`link.list`, `link.create` form,
`link.update` form, `link.enable|disable|delete|restore|preview`); it also
implements the D11 edges `Links -> LinkDetail`, `Links -> LinkForm`,
`LinkForm -> Confirm -> Links`.

## Dependencies

- Internal: 06.0/09-10 (`TableModel`, `make_table_view`), 08 (`FormScreen`, `run_mutation`), 06 (confirm/toast), 03 (`AsyncCaller`).
- Commands: `link.list` (args `status`,`q`,`tag`,`campaign`,`sort`,`order`,`offset`,`limit`; result `{items,offset,limit,total}`), `link.enable|disable|delete|restore|preview`, `link.update`, `link.create` per [command catalog](/docs/commands/README.md) 3.1-3.2.
- `LinkView` JSON keys (verified in `src/app/link_command_service.cpp::serializeLinkViewJson`, in order): `id`, `slug`, `url` (the
  target; the C++ field is `target_url`), `short_url`, `created_at`, `updated_at`, `status`, `redirect_type`, `tags`, `metadata`,
  `campaign`, `stats {total_redirects, redirects_24h, redirects_7d, last_accessed_at}`. Clicks come from `stats.total_redirects`.
  **There is no `expires_at`, `enabled` or `deleted_at` key** in the `LinkView` JSON (they exist only in the C++ struct and in the
  reduced preview body); the D3 `EXPIRES` column therefore shows `-` unless a future additive `expires_at` key is added to
  `serializeLinkViewJson` (that would change REST bodies and needs a C3 decision; recorded as a risk).
- External FTXUI: `Radiobox` (horizontal) or `Toggle(entries, &selected)`, `Input`, plus subtask 10 components.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/links_query.hpp` / `src/tui/links_query.cpp` | create | `parse_filter_text`, `build_link_list_request` (model lib) |
| `include/url_shortener/tui/links_model.hpp` / `src/tui/links_model.cpp` | create | `LinksModel`: columns, status choice, action availability per row (model lib) |
| `src/tui/screens/links_screen.cpp` | create | `LinksScreen : IScreen` (UI lib) |
| `tests/unit/tui/12_links_query_test.cpp`, `12_links_model_test.cpp`, `12_links_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d03_links_80x24.txt`, `d03_links_filter_80x24.txt`, `d03_links_empty_80x24.txt`, `d03_links_error_80x24.txt`, `d03_links_readonly_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
enum class StatusFilter { all, active, disabled, deleted };           // order = D3 radio order
struct LinkListFilter { std::string q, tag, campaign; StatusFilter status = StatusFilter::all; };
LinkListFilter parse_filter_text(const std::string& text, StatusFilter status);
//  "camp:spring" -> campaign; "tag:docs" -> tag; anything else -> q; unknown "x:" prefix -> part of q
commands::CommandRequest build_link_list_request(const LinkListFilter&, const PageQuery&, const std::string& request_id);
//  always sends "status" explicitly (TUI sends all, decision-log Q9); sort limited to created_at|slug|updated_at; limit = page size

enum class LinkAction { detail, create, edit, toggle, remove, restore, preview, stats };
struct ActionAvailability { bool enabled; std::string reason; };
class LinksModel {
public:
    explicit LinksModel(bool read_only);
    ActionAvailability availability(LinkAction a, const std::string& status_of_selected) const;
    //  toggle: active->disable, disabled->enable, deleted->unavailable("restore first"); restore: only deleted; remove: not deleted; read-only: every mutation unavailable with the not_authorized reason
    std::vector<ColumnDef> columns() const;                          // SLUG, TARGET URL, STATUS, CLICKS, EXPIRES
    std::vector<std::string> row_from_link_json(const std::string& link_json) const;
};
class LinksScreen final : public IScreen { /* id() == ScreenId::links; hints() = D3 footer */ };
```

## Implementation steps

1. Query handler on `TableModel` calls `build_link_list_request` with the current `PageQuery` and filter, submits via `AsyncCaller`, maps `items` with `rows_from_result` (keys `slug`,`url`,`status`,`stats.total_redirects`; `expires_at` is mapped if present, else `-`), applies with the model's `generation`.
2. Status radio changes call `set_filter` equivalent (reset offset, refresh). Title row via `TableViewOptions::header_extra` renders `filter: [..]  status: (all) active disabled expired deleted`.
3. Actions (via `on_extra_key`): `n` -> push `link_form` with `link.create` `FormModel`; `e` -> `link.update` form prefilled from `selected_raw()` (slug locked; target URL is not editable because `link.update` has no `target_url` arg - show hint "target URL cannot be changed; create a new link"); `t` -> `run_mutation(link.enable|disable)`; `d` -> `run_mutation(link.delete)` with typed-slug confirm; `u` -> `link.restore`; `p` -> `link.preview` shown in the result viewer (11); `s`/Enter -> push `link_detail` (13) with slug.
4. After any successful mutation: toast + `refresh()` (D12 last step "toast + refreshed table").
5. `on_enter()` refreshes if data older than 30 s; `r` refreshes manually.
6. Row `status` text from JSON `status`: one of `active`, `disabled`, `expired`, `deleted` (verified `LinkStatus` enum in `core/types.hpp`; `link.list` `status` filter accepts the same values plus `all`, catalog 3.2).

## Commands

```powershell
cmake --build cmake-build --target tui__12_links_query_test tui__12_links_model_test tui__12_links_screen_test
ctest --test-dir cmake-build -R "^tui__12_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `12_links_query_test.cpp` | `tui__12_links_query_test` | unit | filter parsing: empty, `camp:spring`, `tag:docs`, mixed words, `camp:` with empty value, unknown prefix, quotes; request bytes golden `07`-style for status all/active; `limit` always 20, `offset` multiples of 20; sort column outside allowed set rejected |
| `12_links_model_test.cpp` | `tui__12_links_model_test` | unit | availability matrix: 4 link statuses (active, disabled, expired, deleted) x 8 actions x read-only on/off; row mapping of a full and a minimal `LinkView` JSON (no expires, no stats) |
| `12_links_screen_test.cpp` | `tui__12_links_screen_test` | unit | first render issues exactly one `link.list` with `status:"all"`; D3 golden from a 4-row mock response incl. long truncated URL and deleted row; `n` opens form; `t` on an active row sends `link.disable` after confirm; `d` requires typed slug then sends `link.delete` then refresh; `u` only on deleted; server error -> error toast and table keeps rows; empty -> "no links"; read-only golden shows write keys dim and sends nothing |

## Constraints

- C8: only `link.*` envelopes; no `LinkView` C++ type.
- ADR 0003 rule 4 confirms; D3 key footer text must match D3.
- Default `status` for the TUI is `all` explicitly (decision-log Q9); the descriptor default `active` is not relied on.
- 80 columns: D3 row layout; wider terminals give extra width to TARGET URL.

## Success criteria

1. `d03_links_80x24.txt` visually equals D3 (header, `>` marker, footer keys).
2. Page text reads `page 1/61  (20 per page)` for total 1204.
3. Each action sends exactly the intended command with `{"slug":...}` and no others (mock asserts).
4. After a mutation the table refreshes once.

## Out of scope / risks

- Bulk select/delete, saved filters, column customisation persistence.
- `link.list` items are full `LinkView` JSON (Task 03.0 subtask 14 serializes with `serializeLinkViewJson`), so `stats.total_redirects` is present; on persistent backends the counter lags by up to the stats flush interval (Task 02.0 subtask 05).
- Risk: the D3 `EXPIRES` column has no source key (see Dependencies); shown as `-`.
