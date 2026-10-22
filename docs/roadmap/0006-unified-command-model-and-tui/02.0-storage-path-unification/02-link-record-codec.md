# 02 - Link <-> record codec

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03, 03.0/12
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Convert losslessly between the application model `url_shortener::Link`
(tags, metadata, campaign, redirect type, stats, enabled flag, soft-delete)
and the pluggable port's `LinkRecord` / `CreateLinkRequest`, which only has
`short_code`, `target_url`, timestamps, `owner`, `is_active` and a
`map<string,string> attributes`. All fields the port cannot hold are packed
into `attributes` under reserved keys. One codec, one place: Task 03.0 can later
replace the packing with real columns without touching callers.

## Dependencies

- `::LinkRecord` (`include/url_shortener/storage/models/link_record.hpp`) and
  `::CreateLinkRequest` (`.../create_link_request.hpp`), both global-namespace,
  verified. `CreateLinkRequest::isValid()` requires non-empty target and a
  short code matching `^[A-Za-z0-9_-]{3,64}$` (looser than `isValidSlug`).
- `url_shortener::formatTimestamp`, `parseRfc3339Zulu` (`core/utils.h`,
  verified) for the string <-> `time_point` conversion (second precision).
- Verified storage facts that dictate the design:
  - `SqlMetadataRowMapper` (`src/storage/sql/sql_metadata_row_mapper.cpp`)
    persists `id` and `attributes` as JSON via `boost::property_tree` using
    `attrs_tree.put(k, v)`: **keys containing `.` are split into nested nodes
    and lost on read**, so attribute keys must contain no dots.
  - The SQL schema (`sqlite_sql_dialect.cpp`) has no `deleted_at` or `version`
    column; `SqlMetadataRowMapper::FromRow` never sets them. `deleted_at`
    must therefore also travel in `attributes`.
  - `InMemoryMetadataRepository::CreateLink` ignores any caller-supplied
    `created_at` and uses the `now` argument; callers pass the link's
    `created_at` as `now`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/app/link_record_codec.hpp` | create | Codec API and attribute key constants |
| `src/app/link_record_codec.cpp` | create | Implementation |
| `tests/unit/app/20_link_record_codec.cpp` | create | Round-trip and failure tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/app/link_record_codec.cpp`; `APP_UNIT_SOURCES` += test |

## Classes and functions

`namespace url_shortener::app`:

```cpp
// Attribute keys (no '.' allowed - property_tree path separator).
inline constexpr std::string_view attr_schema   = "lnk_v";         // "1"
inline constexpr std::string_view attr_enabled  = "lnk_enabled";   // "1" | "0"
inline constexpr std::string_view attr_redirect = "lnk_redirect";  // "temporary" | "permanent"
inline constexpr std::string_view attr_tags     = "lnk_tags";      // pct(tag),pct(tag),...
inline constexpr std::string_view attr_metadata = "lnk_meta";      // pct(k)=pct(v)&pct(k)=pct(v)
inline constexpr std::string_view attr_campaign = "lnk_campaign";  // name=..&source=..  (only present fields)
inline constexpr std::string_view attr_stats    = "lnk_stats";     // total=N&r24=N&r7=N&last=pct(ts)
inline constexpr std::string_view attr_deleted  = "lnk_deleted";   // RFC 3339, present iff soft-deleted

std::string percent_encode(std::string_view in);                 // keeps [A-Za-z0-9-_.~], encodes the rest
std::optional<std::string> percent_decode(std::string_view in);  // nullopt on malformed %XX

/// Link -> port request for a brand-new record (create path).
bool link_to_create_request(const Link& link, ::CreateLinkRequest* out, std::string* error);

/// Link -> full record (update path). deleted_at is written to both
/// LinkRecord::deleted_at and attr_deleted; is_active = enabled && !deleted.
bool link_to_record(const Link& link, ::LinkRecord* out, std::string* error);

/// Record -> Link. Tolerant of records written by other code paths: missing
/// lnk_* keys default to enabled = record.is_active, redirect = temporary,
/// empty tags/metadata/campaign, zero stats. Prefers attr_deleted over
/// LinkRecord::deleted_at when both exist.
bool record_to_link(const ::LinkRecord& record, Link* out, std::string* error);

/// Replace only the stats attribute (used by recordRedirect, subtask 05).
void set_stats_attribute(std::map<std::string, std::string>* attributes,
                         const Link::Stats& stats);
std::optional<Link::Stats> get_stats_attribute(
    const std::map<std::string, std::string>& attributes);
```

Field mapping:

| `Link` | Port | Notes |
|---|---|---|
| `id` | `LinkRecord::id` / `CreateLink(id)` | preserved by the SQL payload `id` |
| `slug` | `short_code` | |
| `target_url` | `target_url` | |
| `created_at`, `updated_at` (RFC 3339 string) | `created_at`, `updated_at` (`time_point`) | parse failure -> codec error |
| `expires_at` | `expires_at` | |
| `deleted_at` | `deleted_at` **and** `lnk_deleted` | SQL drops the former |
| `enabled` | `is_active` (= enabled and not deleted) **and** `lnk_enabled` | disabled vs deleted stay distinguishable |
| `redirect_type` | `lnk_redirect` | |
| `tags`, `metadata`, `campaign` | `lnk_tags`, `lnk_meta`, `lnk_campaign` | percent-encoded tokens |
| `stats` | `lnk_stats` | `last_accessed_at` percent-encoded |
| (none) | `owner` | left empty |
| (none) | `version` | unused (not persisted by SQL) |

Failure modes return `false` with a fixed `error` vocabulary
(`bad_timestamp`, `bad_attribute`, `invalid_request`); the store maps them to
`AppErrorCode::internal` / `storage_failure`.

## Implementation steps

1. Header with the constants (so tests and the store share names).
2. Percent codec (RFC 3986 unreserved set; decode rejects `%` not followed by
   two hex digits).
3. Pack/unpack helpers per attribute, each tolerant of absence and strict on
   malformed present values.
4. `link_to_create_request` (attributes + `expires_at` + `is_active`),
   `link_to_record`, `record_to_link`, stats helpers.
5. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target app__20_link_record_codec
ctest --test-dir cmake-build -R "^app__20_link_record_codec$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/20_link_record_codec.cpp` | `app__20_link_record_codec` | unit | below |

- Full round trip `record_to_link(link_to_record(l)) == l` (field by field) for
  a fully populated link and for a minimal link.
- Hostile values in tags/metadata/campaign: `.`, `&`, `=`, `,`, `%`, `"`, `\`,
  newline, non-ASCII UTF-8, empty string, 2 KB value; keys with `.` in metadata.
- **Through the real SQL path**: write `link_to_record` output via
  `SqlMetadataRowMapper::ToUpdateRow` and read back with `FromRow` (in-process,
  no DB) to prove attributes survive the JSON payload (guards the dot issue).
- Soft delete: `deleted_at` set -> `is_active == false`, `lnk_deleted` present;
  after `FromRow` (which drops `LinkRecord::deleted_at`) the link still reads as
  deleted. Disabled-but-not-deleted stays `enabled == false`, `deleted_at` empty.
- Restore: clearing `deleted_at` removes `lnk_deleted`.
- Tolerance: a record with no `lnk_*` keys and `is_active == false` becomes
  `enabled == false`; unknown extra attributes are ignored and not rewritten
  away (not applicable on read; on write only `lnk_*` keys are produced).
- Failure cases: bad `created_at`; malformed `%` in tags; `lnk_stats` with
  non-numeric counter; each returns `false` with the documented error.
- `percent_encode`/`decode` property: decode(encode(x)) == x over random bytes.
- `link_to_create_request` output passes `CreateLinkRequest::isValid()` for a
  valid slug and fails for a 2-char slug.
- `set_stats_attribute` leaves every other attribute byte-identical.

## Constraints

- Lossless for every `Link` field; second precision for timestamps (documented).
- No JSON library; attribute values are plain strings.
- Attribute keys never contain `.` (asserted in the test over all constants).
- Never log attribute values (tags/metadata may be sensitive).
- C++17; qualify global types with `::` inside `namespace url_shortener::app`
  (`::LinkRecord`, `::CreateLinkRequest`) to avoid the legacy-name clash
  described in the task README.

## Success criteria

1. `app__20_link_record_codec` passes, including the `SqlMetadataRowMapper`
   round trip.
2. A grep of the codec for string keys finds only the `attr_*` constants.
3. The mapping table above is covered by named test cases (one per row).

## Out of scope / risks

- Real columns for `deleted_at`, `enabled`, tags (schema + Postgres migration
  `db/migrations/postgres/`) would let `ListLinks` filter by status in SQL
  instead of in memory. Not owned in this milestone: Task 03.0 subtasks 01 and
  12 filter in the app layer on purpose; an architect decision is needed.
- Attribute size limits of the backend (`attributes_json TEXT`: unbounded in
  SQLite; Postgres `jsonb` column UNVERIFIED) - upstream validators already
  cap tags/metadata sizes.
- Schema evolution: `lnk_v` is written as "1"; readers must ignore higher
  versions they do not understand only by failing the read (decision deferred).
