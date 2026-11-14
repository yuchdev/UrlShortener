#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/models/create_link_request.hpp>
#include <url_shortener/storage/models/link_record.hpp>

namespace url_shortener
{
namespace app
{

// ---------------------------------------------------------------------------
// Attribute keys
//
// Every field of `url_shortener::Link` that the pluggable port
// (`::LinkRecord` / `::CreateLinkRequest`) cannot represent natively is
// packed into the record's `attributes` map under one of the reserved keys
// below. No key may contain `.`: `SqlMetadataRowMapper` serializes
// `attributes` with `boost::property_tree::ptree::put`, which splits keys on
// `.` into nested nodes that are then lost on read. The keys are therefore
// chosen from `[A-Za-z0-9_]` only and this invariant is asserted by the unit
// test over every constant below.
//
// Only these `lnk_*` keys are ever produced on write; any other attribute on
// a record read back is ignored and left untouched (the codec never rewrites
// a record it did not pack).
// ---------------------------------------------------------------------------

///< Codec schema version, always written as `"1"`. `record_to_link` fails
///< closed (`bad_attribute`) on any present-but-unrecognized value so a
///< record written by a future, incompatible codec is never misread as the
///< current format (threat model T-37).
inline constexpr std::string_view attr_schema = "lnk_v";

///< Link `enabled` flag, written as `"1"` or `"0"`. Kept distinct from the
///< record's `is_active` column so a disabled link and a soft-deleted link
///< (both `is_active == false`) stay distinguishable on read.
inline constexpr std::string_view attr_enabled = "lnk_enabled";

///< Redirect semantics, written as `"temporary"` or `"permanent"`.
inline constexpr std::string_view attr_redirect = "lnk_redirect";

///< Tag list, written as `pct(tag)` values joined by `,`. Absent when the
///< link has no tags (distinct from a single empty-string tag, which is the
///< one-element value `""`).
inline constexpr std::string_view attr_tags = "lnk_tags";

///< Metadata map, written as `pct(key)=pct(value)` pairs joined by `&`.
///< Absent when the link has no metadata.
inline constexpr std::string_view attr_metadata = "lnk_meta";

///< Campaign attribution, written as `field=pct(value)` pairs joined by `&`
///< for the present fields only (`name`, `source`, `medium`, `term`,
///< `content`, `id`). Present (possibly as an empty value) iff the link
///< carries a campaign.
inline constexpr std::string_view attr_campaign = "lnk_campaign";

///< Redirect counters, written as `total=N&r24=N&r7=N` with an optional
///< trailing `&last=pct(timestamp)` when a last-access time is recorded.
inline constexpr std::string_view attr_stats = "lnk_stats";

///< Soft-delete timestamp, written verbatim as the link's RFC 3339
///< `deleted_at` string; present iff the link is soft-deleted. The SQL
///< schema has no `deleted_at` column, so this attribute is the only place
///< soft-delete survives a SQL round trip. Stored unencoded because it is a
///< machine-generated standalone scalar (no embedded separators to escape).
inline constexpr std::string_view attr_deleted = "lnk_deleted";

// ---------------------------------------------------------------------------
// Error vocabulary (written to the `error` out-param of the mapping
// functions on failure). The store maps these to `AppErrorCode::internal` /
// `AppErrorCode::storage_failure`.
// ---------------------------------------------------------------------------

///< A timestamp string (`created_at`, `updated_at`, `expires_at`,
///< `deleted_at`) did not parse as a strict RFC 3339 Zulu value.
inline constexpr std::string_view error_bad_timestamp = "bad_timestamp";

///< A packed `lnk_*` attribute value was present but malformed: a `%`
///< percent-escape that is not followed by two hex digits, a non-numeric
///< stats counter, or an unrecognized `lnk_v` schema version.
inline constexpr std::string_view error_bad_attribute = "bad_attribute";

///< A required output pointer was null, or the input could not form a
///< request/record at all.
inline constexpr std::string_view error_invalid_request = "invalid_request";

/**
 * @brief Percent-encodes `in` per the RFC 3986 unreserved set.
 *
 * Every byte in `[A-Za-z0-9-_.~]` is emitted verbatim; every other byte
 * (including all separators the codec uses - `,`, `&`, `=` - as well as
 * `%`, control bytes and non-ASCII) is emitted as `%XX` with uppercase hex.
 * The result is therefore safe to embed inside the `,`/`&`/`=`-separated
 * composite attribute values and inside the `property_tree` JSON payload.
 * @param in Raw bytes to encode; treated as opaque (may contain any byte).
 * @return The percent-encoded form, containing only `[A-Za-z0-9-_.~%]`.
 */
std::string percent_encode(std::string_view in);

/**
 * @brief Reverses `percent_encode`.
 *
 * Non-escaped bytes pass through unchanged; each `%XX` is decoded from two
 * hexadecimal digits. A `%` that is not followed by exactly two hex digits
 * is malformed and rejected.
 *
 * @warning The decode is byte-transparent by design (it must round-trip
 * anything the write path accepts), so when `in` comes from a `::LinkRecord`
 * the returned bytes are untrusted: they may contain embedded NUL bytes or
 * invalid UTF-8 placed there by a foreign writer. Callers must sanitize the
 * result before logging it or rendering it into any downstream sink.
 * @param in Percent-encoded input.
 * @return The decoded bytes, or `std::nullopt` if `in` contains a `%` not
 * followed by two hex digits.
 */
std::optional<std::string> percent_decode(std::string_view in);

/**
 * @brief Maps a `Link` to a `::CreateLinkRequest` for the create path.
 *
 * Fills `short_code`, `target_url`, `expires_at`, `is_active`
 * (`= enabled && not soft-deleted`) and the packed `lnk_*` `attributes`.
 * `owner` is left empty and the link `id` is not carried here (the caller
 * passes it to `IMetadataRepository::CreateLink` separately). This function
 * does not enforce slug validity; the caller checks
 * `::CreateLinkRequest::isValid()` on the result.
 * @param link Fully-formed, already-validated link to map. The codec
 * performs no URL/slug re-validation (threat model T-40: validation stays
 * upstream in `LinkCommandService`).
 * @param out Receives the request on success; left unchanged on failure.
 * Must not be null.
 * @param error If non-null, receives `error_bad_timestamp` when any of
 * `expires_at`/`deleted_at` fails to parse as RFC 3339, or
 * `error_invalid_request` when `out` is null; untouched on success.
 * @return `true` on success; `false` if `out` is null or a timestamp failed
 * to parse.
 */
bool link_to_create_request(const Link& link,
                            ::CreateLinkRequest* out,
                            std::string* error);

/**
 * @brief Maps a `Link` to a full `::LinkRecord` for the update path.
 *
 * `deleted_at` is written to both `::LinkRecord::deleted_at` and the
 * `attr_deleted` attribute, because the SQL schema drops the former;
 * `is_active` is `enabled && not soft-deleted`. `owner` is left empty and
 * `version` keeps its default (not persisted by the SQL backend).
 * @param link Fully-formed, already-validated link to map.
 * @param out Receives the record on success; left unchanged on failure.
 * Must not be null.
 * @param error If non-null, receives `error_bad_timestamp` when any of
 * `created_at`/`updated_at`/`expires_at`/`deleted_at` fails to parse as
 * RFC 3339, or `error_invalid_request` when `out` is null; untouched on
 * success.
 * @return `true` on success; `false` if `out` is null or a timestamp failed
 * to parse.
 */
bool link_to_record(const Link& link, ::LinkRecord* out, std::string* error);

/**
 * @brief Maps a `::LinkRecord` back to a `Link`.
 *
 * Tolerant of records written by other code paths: a missing `lnk_*` key
 * defaults `enabled` to `record.is_active`, `redirect_type` to
 * `temporary`, and leaves tags/metadata/campaign empty and stats zero.
 * `deleted_at` prefers `attr_deleted` over `::LinkRecord::deleted_at` (so a
 * soft-delete survives even though the SQL mapper drops the column). Unknown
 * non-`lnk_*` attributes are ignored. Fails closed on data it cannot trust.
 *
 * Because a `::LinkRecord` may be read back from storage a foreign process
 * wrote (see the storage-path-unification threat model), the packed values
 * are treated as untrusted and bounded on read: the tag/metadata/campaign
 * entry counts and the key/value lengths are capped to the same limits the
 * write path validates (`validateTags`/`validateMetadata`/`validateCampaign`
 * in `core/utils`), and the `attr_deleted` and stats `last` timestamps must
 * parse as strict RFC 3339 Zulu. Exceeding any cap or failing any parse
 * yields `error_bad_attribute`.
 *
 * @warning Decode is byte-transparent, so the decoded tag/metadata/campaign
 * values (and `target_url`) may still contain embedded NUL bytes or invalid
 * UTF-8 originating from a foreign record even when this call succeeds.
 * Consumers must sanitize these fields before logging or rendering them.
 * @param record Record to map (from any backend).
 * @param out Receives the link on success; left in an indeterminate state on
 * failure. Must not be null.
 * @param error If non-null, receives `error_bad_attribute` when a present
 * `lnk_*` value is malformed (bad percent-escape, non-numeric stats counter,
 * an `attr_schema` value other than `"1"`, an entry count or key/value length
 * past the write-path caps, or an unparseable `attr_deleted`/stats-`last`
 * timestamp), or `error_invalid_request` when `out` is null; untouched on
 * success.
 * @return `true` on success; `false` if `out` is null or a present `lnk_*`
 * attribute is malformed, over a cap, or carries an unsupported schema
 * version.
 */
bool record_to_link(const ::LinkRecord& record, Link* out, std::string* error);

/**
 * @brief Replaces only the `attr_stats` entry of `attributes` in place.
 *
 * Used by the write-behind redirect-stats flush (subtask 05) to persist a
 * counter update without re-packing the whole link. Every other key in
 * `attributes` is left byte-identical.
 * @param attributes Attribute map to mutate; must not be null. The
 * `attr_stats` key is inserted or overwritten; no other key is touched.
 * @param stats Counters to encode.
 */
void set_stats_attribute(std::map<std::string, std::string>* attributes,
                         const Link::Stats& stats);

/**
 * @brief Reads the `attr_stats` entry of `attributes`, if present and valid.
 * @param attributes Attribute map to read.
 * @return The decoded counters, or `std::nullopt` when `attr_stats` is
 * absent or malformed (unlike `record_to_link`, this accessor has no error
 * channel and reports a malformed value as absent).
 */
std::optional<Link::Stats> get_stats_attribute(
    const std::map<std::string, std::string>& attributes);

}  // namespace app
}  // namespace url_shortener
