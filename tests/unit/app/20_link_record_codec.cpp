/**
 * @file 20_link_record_codec.cpp
 * @brief Unit tests for app::link_record_codec: lossless Link<->LinkRecord
 * round trips (including through the real SqlMetadataRowMapper), hostile
 * attribute values, soft-delete/disable/restore semantics, read tolerance,
 * fail-closed schema handling (threat model T-37), the percent codec, and
 * the stats-attribute helpers.
 */
#define BOOST_TEST_MODULE LinkRecordCodec
#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/models/create_link_request.hpp>
#include <url_shortener/storage/models/link_record.hpp>
#include <url_shortener/storage/sql/sql_metadata_row_mapper.hpp>

using namespace url_shortener;
using namespace url_shortener::app;

namespace
{

/**
 * @brief Builds a fully populated link exercising every codec field.
 * @return A link with tags, metadata, campaign, stats, expiry and all
 * scalar fields set.
 */
Link makeFullLink()
{
    Link link;
    link.id = "id-1234";
    link.slug = "promo-link";
    link.target_url = "https://example.com/landing?x=1&y=2";
    link.created_at = "2026-01-02T03:04:05Z";
    link.updated_at = "2026-01-02T06:07:08Z";
    link.expires_at = "2026-12-31T23:59:59Z";
    link.enabled = true;
    link.tags = {"alpha", "beta gamma", "x,y", "k=v"};
    link.metadata = {{"owner", "team-a"}, {"note", "a&b=c"}};
    Link::Campaign campaign;
    campaign.name = "spring";
    campaign.source = "news letter";
    campaign.medium = "email";
    campaign.term = "sale%";
    campaign.content = "banner,1";
    campaign.id = "cmp-9";
    link.campaign = campaign;
    link.stats.total_redirects = 123456789;
    link.stats.redirects_24h = 42;
    link.stats.redirects_7d = 999;
    link.stats.last_accessed_at = "2026-02-03T04:05:06Z";
    link.redirect_type = RedirectType::permanent;
    return link;
}

/**
 * @brief Builds a minimal link with only the required scalar fields set.
 * @return A link with empty tags/metadata, no campaign, zero stats.
 */
Link makeMinimalLink()
{
    Link link;
    link.id = "id-min";
    link.slug = "min";
    link.target_url = "https://example.com/";
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = "2026-01-01T00:00:00Z";
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

/**
 * @brief Asserts two optional campaigns are field-by-field equal.
 * @param expected Expected campaign.
 * @param actual Actual campaign.
 */
void checkCampaignEqual(const std::optional<Link::Campaign>& expected,
                        const std::optional<Link::Campaign>& actual)
{
    BOOST_REQUIRE_EQUAL(expected.has_value(), actual.has_value());
    if (!expected.has_value()) {
        return;
    }
    BOOST_CHECK(expected->name == actual->name);
    BOOST_CHECK(expected->source == actual->source);
    BOOST_CHECK(expected->medium == actual->medium);
    BOOST_CHECK(expected->term == actual->term);
    BOOST_CHECK(expected->content == actual->content);
    BOOST_CHECK(expected->id == actual->id);
}

/**
 * @brief Asserts two links are equal across every codec-carried field.
 * @param expected Expected link.
 * @param actual Actual link.
 */
void checkLinkEqual(const Link& expected, const Link& actual)
{
    BOOST_CHECK_EQUAL(expected.id, actual.id);
    BOOST_CHECK_EQUAL(expected.slug, actual.slug);
    BOOST_CHECK_EQUAL(expected.target_url, actual.target_url);
    BOOST_CHECK_EQUAL(expected.created_at, actual.created_at);
    BOOST_CHECK_EQUAL(expected.updated_at, actual.updated_at);
    BOOST_CHECK(expected.expires_at == actual.expires_at);
    BOOST_CHECK(expected.deleted_at == actual.deleted_at);
    BOOST_CHECK_EQUAL(expected.enabled, actual.enabled);
    BOOST_CHECK(expected.tags == actual.tags);
    BOOST_CHECK(expected.metadata == actual.metadata);
    checkCampaignEqual(expected.campaign, actual.campaign);
    BOOST_CHECK_EQUAL(expected.stats.total_redirects,
                      actual.stats.total_redirects);
    BOOST_CHECK_EQUAL(expected.stats.redirects_24h, actual.stats.redirects_24h);
    BOOST_CHECK_EQUAL(expected.stats.redirects_7d, actual.stats.redirects_7d);
    BOOST_CHECK(expected.stats.last_accessed_at
                == actual.stats.last_accessed_at);
    BOOST_CHECK(expected.redirect_type == actual.redirect_type);
}

}  // namespace

/**
 * [Unit][App] Every attribute key is free of the property_tree path
 * separator `.`.
 *
 * Scenario:
 *   Given the reserved attr_* key constants.
 *   When each is scanned for a '.'.
 *   Then none contains one (SqlMetadataRowMapper would otherwise split and
 *   lose the key).
 *
 * If this breaks, first check:
 *   - a newly added attr_* constant introduced a dot.
 */
BOOST_AUTO_TEST_CASE(attribute_keys_contain_no_dot)
{
    const std::vector<std::string_view> keys = {attr_schema,
                                                attr_enabled,
                                                attr_redirect,
                                                attr_tags,
                                                attr_metadata,
                                                attr_campaign,
                                                attr_stats,
                                                attr_deleted};
    for (const auto key : keys) {
        BOOST_CHECK(key.find('.') == std::string_view::npos);
    }
}

/**
 * [Unit][App] Full round trip record_to_link(link_to_record(l)) == l for a
 * fully populated link.
 *
 * Scenario:
 *   Given a link with tags, metadata, campaign, stats and expiry.
 *   When mapped to a record and back.
 *   Then every field matches.
 *
 * If this breaks, first check:
 *   - pack_attributes / the per-attribute decoders.
 */
BOOST_AUTO_TEST_CASE(full_round_trip)
{
    const Link original = makeFullLink();
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));
    BOOST_CHECK(error.empty());

    Link restored;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    checkLinkEqual(original, restored);
}

/**
 * [Unit][App] Full round trip for a minimal link (no tags/metadata/campaign,
 * zero stats).
 *
 * Scenario:
 *   Given a minimal link.
 *   When mapped to a record and back.
 *   Then every field matches and optional collections stay empty.
 *
 * If this breaks, first check:
 *   - empty-collection omission vs default-on-read.
 */
BOOST_AUTO_TEST_CASE(minimal_round_trip)
{
    const Link original = makeMinimalLink();
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));

    Link restored;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    checkLinkEqual(original, restored);
    BOOST_CHECK(restored.tags.empty());
    BOOST_CHECK(restored.metadata.empty());
    BOOST_CHECK(!restored.campaign.has_value());
}

/**
 * [Unit][App] Hostile values in tags/metadata/campaign survive a round trip.
 *
 * Scenario:
 *   Given values containing '.', '&', '=', ',', '%', '"', '\\', newline,
 *   non-ASCII UTF-8, the empty string, an at-cap-length value, and a metadata
 *   key with a '.'.
 *   When mapped to a record and back.
 *   Then every value is preserved exactly.
 *
 * Boundaries:
 *   - The longest tag/metadata-value/campaign-field values are sized exactly
 *     at the read-path caps (32/512/128 bytes) that record_to_link enforces,
 *     mirroring the upstream write-path validators; values past those caps are
 *     rejected by record_to_link (see over_cap tests below) and the write path
 *     never produces them, so they are not exercised here.
 *
 * If this breaks, first check:
 *   - percent_encode coverage of separators and the split_bounded() semantics.
 */
BOOST_AUTO_TEST_CASE(hostile_values_round_trip)
{
    Link original = makeMinimalLink();
    original.tags = {".",
                     "&",
                     "=",
                     ",",
                     "%",
                     "\"",
                     "\\",
                     "line1\nline2",
                     "caf\xC3\xA9",
                     "",
                     std::string(32, 'Z')};
    original.metadata = {{"dotted.key", "v1"},
                         {"k&=,", "val with %25 and \"q\""},
                         {"", "empty-key-value"},
                         {"big", std::string(512, 'Q')}};
    Link::Campaign campaign;
    campaign.name = "a.b&c=d,e%f\"g\\h";
    campaign.source = "caf\xC3\xA9";
    campaign.content = std::string(128, 'C');
    original.campaign = campaign;

    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));

    Link restored;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    checkLinkEqual(original, restored);
}

/**
 * [Unit][App] Attributes survive the real SqlMetadataRowMapper JSON payload
 * (guards the property_tree dot-splitting hazard).
 *
 * Scenario:
 *   Given a fully populated link with a dotted metadata key.
 *   When mapped to a record, serialized via ToUpdateRow and read back with
 *   FromRow (in-process, no DB), then decoded back to a link.
 *   Then every field matches.
 *
 * If this breaks, first check:
 *   - an attribute key containing '.', or a value that breaks JSON.
 */
BOOST_AUTO_TEST_CASE(round_trip_through_sql_mapper)
{
    Link original = makeFullLink();
    original.metadata["nested.looking.key"] = "still one value";

    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));

    const SqlMetadataRowMapper mapper;
    const SqlRow row = mapper.ToUpdateRow(record);

    ::LinkRecord read_back;
    ::RepoError repo_error = ::RepoError::none;
    BOOST_REQUIRE(mapper.FromRow(row, &read_back, &repo_error));
    BOOST_CHECK(repo_error == ::RepoError::none);

    Link restored;
    BOOST_REQUIRE(record_to_link(read_back, &restored, &error));
    checkLinkEqual(original, restored);
}

/**
 * [Unit][App] A soft-deleted link maps to is_active == false, writes
 * lnk_deleted, and still reads as deleted after FromRow drops the column.
 *
 * Scenario:
 *   Given an enabled link with deleted_at set.
 *   When mapped to a record, round-tripped through the SQL mapper (which
 *   drops LinkRecord::deleted_at), and decoded back.
 *   Then is_active is false, lnk_deleted is present, and the restored link
 *   reads as deleted with enabled still true.
 *
 * If this breaks, first check:
 *   - is_active = enabled && !deleted; attr_deleted preferred on read.
 */
BOOST_AUTO_TEST_CASE(soft_delete_survives_sql_column_drop)
{
    Link original = makeMinimalLink();
    original.enabled = true;
    original.deleted_at = "2026-03-04T05:06:07Z";

    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));
    BOOST_CHECK(!record.is_active);
    BOOST_CHECK(record.attributes.count(std::string(attr_deleted)) == 1);
    BOOST_CHECK(record.deleted_at.has_value());

    const SqlMetadataRowMapper mapper;
    ::LinkRecord read_back;
    ::RepoError repo_error = ::RepoError::none;
    BOOST_REQUIRE(
        mapper.FromRow(mapper.ToUpdateRow(record), &read_back, &repo_error));
    BOOST_CHECK(!read_back.deleted_at.has_value());  // column dropped by SQL

    Link restored;
    BOOST_REQUIRE(record_to_link(read_back, &restored, &error));
    BOOST_CHECK(restored.deleted_at == original.deleted_at);
    BOOST_CHECK(restored.enabled);
}

/**
 * [Unit][App] A disabled-but-not-deleted link stays enabled == false with no
 * deleted_at, distinct from soft delete.
 *
 * Scenario:
 *   Given a link with enabled == false and no deleted_at.
 *   When round-tripped.
 *   Then enabled is false, deleted_at stays empty, and lnk_deleted is absent.
 *
 * If this breaks, first check:
 *   - attr_enabled distinct from is_active.
 */
BOOST_AUTO_TEST_CASE(disabled_not_deleted_distinguishable)
{
    Link original = makeMinimalLink();
    original.enabled = false;

    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));
    BOOST_CHECK(!record.is_active);
    BOOST_CHECK(record.attributes.count(std::string(attr_deleted)) == 0);

    Link restored;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    BOOST_CHECK(!restored.enabled);
    BOOST_CHECK(!restored.deleted_at.has_value());
}

/**
 * [Unit][App] Restoring a link (clearing deleted_at) removes lnk_deleted.
 *
 * Scenario:
 *   Given a link that was soft-deleted and then restored (deleted_at empty).
 *   When mapped to a record.
 *   Then lnk_deleted is absent and is_active follows enabled.
 *
 * If this breaks, first check:
 *   - pack_attributes only writes attr_deleted when deleted_at is present.
 */
BOOST_AUTO_TEST_CASE(restore_clears_lnk_deleted)
{
    Link original = makeMinimalLink();
    original.enabled = true;
    original.deleted_at.reset();

    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(original, &record, &error));
    BOOST_CHECK(record.attributes.count(std::string(attr_deleted)) == 0);
    BOOST_CHECK(record.is_active);
}

/**
 * [Unit][App] A record with no lnk_* keys and is_active == false decodes to
 * enabled == false; unknown extra attributes are ignored.
 *
 * Scenario:
 *   Given a bare record (foreign code path) with is_active false and one
 *   unknown attribute.
 *   When decoded to a link.
 *   Then enabled follows is_active, redirect defaults to temporary, and the
 *   unknown key does not surface.
 *
 * If this breaks, first check:
 *   - read tolerance defaults and unknown-key handling.
 */
BOOST_AUTO_TEST_CASE(tolerant_of_foreign_record)
{
    ::LinkRecord record;
    record.id = "x";
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.is_active = false;
    record.attributes["some_other_key"] = "ignored";

    Link restored;
    std::string error;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    BOOST_CHECK(!restored.enabled);
    BOOST_CHECK(restored.redirect_type == RedirectType::temporary);
    BOOST_CHECK(restored.tags.empty());
    BOOST_CHECK(restored.metadata.empty());
}

/**
 * [Unit][App] record_to_link fails closed on an unsupported lnk_v schema
 * version (threat model T-37).
 *
 * Scenario:
 *   Given a record whose lnk_v is "2".
 *   When decoded.
 *   Then it returns false with error_bad_attribute rather than trusting the
 *   remaining fields.
 *
 * If this breaks, first check:
 *   - the schema-version guard at the top of record_to_link.
 */
BOOST_AUTO_TEST_CASE(fail_closed_on_unknown_schema_version)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.attributes[std::string(attr_schema)] = "2";

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(record, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));
}

/**
 * [Unit][App] link_to_record rejects an unparseable created_at.
 *
 * Scenario:
 *   Given a link with created_at not in RFC 3339 Zulu form.
 *   When mapped to a record.
 *   Then it returns false with error_bad_timestamp.
 *
 * If this breaks, first check:
 *   - the parseRfc3339Zulu guards in link_to_record.
 */
BOOST_AUTO_TEST_CASE(fail_on_bad_created_at)
{
    Link link = makeMinimalLink();
    link.created_at = "not-a-timestamp";

    ::LinkRecord record;
    std::string error;
    BOOST_CHECK(!link_to_record(link, &record, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_timestamp));
}

/**
 * [Unit][App] record_to_link rejects a malformed percent-escape in tags.
 *
 * Scenario:
 *   Given a record whose lnk_tags value contains "%zz".
 *   When decoded.
 *   Then it returns false with error_bad_attribute.
 *
 * If this breaks, first check:
 *   - percent_decode rejection and decode_tags error propagation.
 */
BOOST_AUTO_TEST_CASE(fail_on_malformed_percent_in_tags)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.attributes[std::string(attr_schema)] = "1";
    record.attributes[std::string(attr_tags)] = "ok,%zz";

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(record, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));
}

/**
 * [Unit][App] record_to_link rejects a non-numeric stats counter.
 *
 * Scenario:
 *   Given a record whose lnk_stats "total" field is non-numeric.
 *   When decoded.
 *   Then it returns false with error_bad_attribute.
 *
 * If this breaks, first check:
 *   - parse_u64 strictness and decode_stats error propagation.
 */
BOOST_AUTO_TEST_CASE(fail_on_non_numeric_stats_counter)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.attributes[std::string(attr_stats)] = "total=12x&r24=0&r7=0";

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(record, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));
}

/**
 * [Unit][App] percent_decode(percent_encode(x)) == x over random bytes.
 *
 * Scenario:
 *   Given random byte strings including all 256 byte values.
 *   When encoded and decoded.
 *   Then the result equals the input and the encoding uses only the safe
 *   alphabet.
 *
 * If this breaks, first check:
 *   - the unreserved-set definition and the hex emit/parse.
 */
BOOST_AUTO_TEST_CASE(percent_codec_round_trips_random_bytes)
{
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::uniform_int_distribution<int> len_dist(0, 64);
    for (int iter = 0; iter < 500; ++iter) {
        std::string input;
        const int len = len_dist(rng);
        for (int i = 0; i < len; ++i) {
            input.push_back(static_cast<char>(byte_dist(rng)));
        }
        const std::string encoded = percent_encode(input);
        const bool safe = std::all_of(
            encoded.begin(),
            encoded.end(),
            [](const unsigned char c)
            {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                    || (c >= '0' && c <= '9') || c == '-' || c == '_'
                    || c == '.' || c == '~' || c == '%';
            });
        BOOST_CHECK(safe);
        const auto decoded = percent_decode(encoded);
        BOOST_REQUIRE(decoded.has_value());
        BOOST_CHECK(*decoded == input);
    }
}

/**
 * [Unit][App] percent_decode rejects a truncated or non-hex escape.
 *
 * Scenario:
 *   Given inputs ending in a bare '%' or containing "%g0".
 *   When decoded.
 *   Then the result is nullopt.
 *
 * If this breaks, first check:
 *   - the bounds and hex checks in percent_decode.
 */
BOOST_AUTO_TEST_CASE(percent_decode_rejects_malformed)
{
    BOOST_CHECK(!percent_decode("abc%").has_value());
    BOOST_CHECK(!percent_decode("abc%1").has_value());
    BOOST_CHECK(!percent_decode("%g0").has_value());
    BOOST_CHECK(!percent_decode("%1z").has_value());
    BOOST_CHECK(percent_decode("%41").has_value());
}

/**
 * [Unit][App] link_to_create_request output passes isValid() for a valid
 * slug and fails for a 2-char slug.
 *
 * Scenario:
 *   Given a link with a valid slug and one with a 2-char slug.
 *   When mapped to a create request.
 *   Then the mapping succeeds in both cases and the resulting request is
 *   valid only for the valid slug; is_active follows enabled && !deleted.
 *
 * If this breaks, first check:
 *   - CreateLinkRequest field population and is_active computation.
 */
BOOST_AUTO_TEST_CASE(create_request_validity_follows_slug)
{
    const Link valid = makeFullLink();
    ::CreateLinkRequest request;
    std::string error;
    BOOST_REQUIRE(link_to_create_request(valid, &request, &error));
    BOOST_CHECK(request.isValid());
    BOOST_CHECK_EQUAL(request.short_code, valid.slug);
    BOOST_CHECK_EQUAL(request.target_url, valid.target_url);
    BOOST_CHECK(request.is_active);
    BOOST_CHECK(request.attributes.count(std::string(attr_stats)) == 1);

    Link short_slug = makeMinimalLink();
    short_slug.slug = "ab";
    ::CreateLinkRequest short_request;
    BOOST_REQUIRE(link_to_create_request(short_slug, &short_request, &error));
    BOOST_CHECK(!short_request.isValid());
}

/**
 * [Unit][App] link_to_create_request marks a soft-deleted link inactive.
 *
 * Scenario:
 *   Given a link with deleted_at set.
 *   When mapped to a create request.
 *   Then is_active is false and lnk_deleted is packed.
 *
 * If this breaks, first check:
 *   - is_active computation on the create path.
 */
BOOST_AUTO_TEST_CASE(create_request_deleted_is_inactive)
{
    Link link = makeMinimalLink();
    link.deleted_at = "2026-03-04T05:06:07Z";

    ::CreateLinkRequest request;
    std::string error;
    BOOST_REQUIRE(link_to_create_request(link, &request, &error));
    BOOST_CHECK(!request.is_active);
    BOOST_CHECK(request.attributes.count(std::string(attr_deleted)) == 1);
}

/**
 * [Unit][App] On write only lnk_* keys are produced.
 *
 * Scenario:
 *   Given a fully populated link.
 *   When mapped to a record.
 *   Then every attribute key begins with "lnk_".
 *
 * If this breaks, first check:
 *   - pack_attributes key constants.
 */
BOOST_AUTO_TEST_CASE(only_lnk_keys_written)
{
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(makeFullLink(), &record, &error));
    for (const auto& [key, value] : record.attributes) {
        BOOST_CHECK_MESSAGE(key.rfind("lnk_", 0) == 0,
                            "unexpected attribute key: " + key);
    }
}

/**
 * [Unit][App] set_stats_attribute replaces only lnk_stats, byte-for-byte
 * preserving every other attribute, and get_stats_attribute reads it back.
 *
 * Scenario:
 *   Given a packed attribute map.
 *   When set_stats_attribute writes new counters.
 *   Then only lnk_stats changes and get_stats_attribute returns the new
 *   counters; an absent lnk_stats reads as nullopt.
 *
 * If this breaks, first check:
 *   - set_stats_attribute touching only the attr_stats key.
 */
BOOST_AUTO_TEST_CASE(stats_helpers_are_surgical)
{
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE(link_to_record(makeFullLink(), &record, &error));
    std::map<std::string, std::string> before = record.attributes;

    Link::Stats new_stats;
    new_stats.total_redirects = 7;
    new_stats.redirects_24h = 3;
    new_stats.redirects_7d = 5;
    new_stats.last_accessed_at = "2026-05-05T05:05:05Z";
    set_stats_attribute(&record.attributes, new_stats);

    for (const auto& [key, value] : before) {
        if (key == std::string(attr_stats)) {
            continue;
        }
        BOOST_CHECK_EQUAL(record.attributes.at(key), value);
    }
    BOOST_CHECK_EQUAL(before.size(), record.attributes.size());

    const auto read = get_stats_attribute(record.attributes);
    BOOST_REQUIRE(read.has_value());
    BOOST_CHECK_EQUAL(read->total_redirects, 7u);
    BOOST_CHECK_EQUAL(read->redirects_24h, 3u);
    BOOST_CHECK_EQUAL(read->redirects_7d, 5u);
    BOOST_CHECK(read->last_accessed_at
                == std::optional<std::string>("2026-05-05T05:05:05Z"));

    std::map<std::string, std::string> empty;
    BOOST_CHECK(!get_stats_attribute(empty).has_value());
}

/**
 * [Unit][App] record_to_link prefers attr_deleted over a set
 * LinkRecord::deleted_at column when both are present.
 *
 * Scenario:
 *   Given a record with both deleted_at (column) and lnk_deleted set to
 *   different timestamps.
 *   When decoded.
 *   Then the link's deleted_at equals the attribute value.
 *
 * If this breaks, first check:
 *   - the attr_deleted-preferred branch in record_to_link.
 */
BOOST_AUTO_TEST_CASE(deleted_attribute_preferred_over_column)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.is_active = false;
    record.deleted_at =
        std::chrono::system_clock::time_point(std::chrono::seconds(1000));
    record.attributes[std::string(attr_deleted)] = "2026-07-08T09:10:11Z";

    Link restored;
    std::string error;
    BOOST_REQUIRE(record_to_link(record, &restored, &error));
    BOOST_CHECK(restored.deleted_at
                == std::optional<std::string>("2026-07-08T09:10:11Z"));
}

/**
 * @brief Builds an `attr_tags` value with `count` comma-separated `t<i>` tags.
 * @param count Number of tags to emit.
 * @return The joined tag attribute value.
 */
std::string makeTagsAttr(const std::size_t count)
{
    std::string value;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            value += ',';
        }
        value += "t" + std::to_string(i);
    }
    return value;
}

/**
 * @brief Builds an `attr_metadata` value with `count` `k<i>=v<i>` entries.
 * @param count Number of entries to emit.
 * @return The joined metadata attribute value.
 */
std::string makeMetadataAttr(const std::size_t count)
{
    std::string value;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            value += '&';
        }
        value += "k" + std::to_string(i) + "=v" + std::to_string(i);
    }
    return value;
}

/**
 * [Unit][App] record_to_link rejects a tag list past the write-path cap and
 * accepts one exactly at it.
 *
 * Scenario:
 *   Given a record (from a possibly foreign writer) whose lnk_tags carries 21
 *   tags, and a second whose lnk_tags carries exactly 20.
 *   When each is decoded.
 *   Then the 21-tag record fails with error_bad_attribute while the 20-tag
 *   record decodes to 20 tags.
 *
 * Boundaries:
 *   - 20 is the validateTags entry cap mirrored by k_max_tags; 21 is the
 *     first value over it.
 *
 * If this breaks, first check:
 *   - split_bounded's early-bail count and decode_tags' cap wiring.
 */
BOOST_AUTO_TEST_CASE(fail_on_too_many_tags)
{
    ::LinkRecord over;
    over.short_code = "abc";
    over.target_url = "https://example.com/";
    over.attributes[std::string(attr_tags)] = makeTagsAttr(21);

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(over, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));

    ::LinkRecord at_limit;
    at_limit.short_code = "abc";
    at_limit.target_url = "https://example.com/";
    at_limit.attributes[std::string(attr_tags)] = makeTagsAttr(20);

    Link ok;
    error.clear();
    BOOST_REQUIRE(record_to_link(at_limit, &ok, &error));
    BOOST_CHECK_EQUAL(ok.tags.size(), 20u);
}

/**
 * [Unit][App] record_to_link rejects a metadata map past the write-path cap
 * and accepts one exactly at it.
 *
 * Scenario:
 *   Given a record whose lnk_meta carries 51 entries, and a second whose
 *   lnk_meta carries exactly 50.
 *   When each is decoded.
 *   Then the 51-entry record fails with error_bad_attribute while the
 *   50-entry record decodes to 50 metadata pairs.
 *
 * Boundaries:
 *   - 50 is the validateMetadata entry cap mirrored by k_max_metadata_entries;
 *     51 is the first value over it.
 *
 * If this breaks, first check:
 *   - split_bounded's early-bail count and decode_metadata's cap wiring.
 */
BOOST_AUTO_TEST_CASE(fail_on_too_many_metadata_entries)
{
    ::LinkRecord over;
    over.short_code = "abc";
    over.target_url = "https://example.com/";
    over.attributes[std::string(attr_metadata)] = makeMetadataAttr(51);

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(over, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));

    ::LinkRecord at_limit;
    at_limit.short_code = "abc";
    at_limit.target_url = "https://example.com/";
    at_limit.attributes[std::string(attr_metadata)] = makeMetadataAttr(50);

    Link ok;
    error.clear();
    BOOST_REQUIRE(record_to_link(at_limit, &ok, &error));
    BOOST_CHECK_EQUAL(ok.metadata.size(), 50u);
}

/**
 * [Unit][App] record_to_link rejects a non-RFC-3339 lnk_deleted attribute.
 *
 * Scenario:
 *   Given a record whose lnk_deleted is "not-a-timestamp".
 *   When decoded.
 *   Then it returns false with error_bad_attribute (the attribute is stored
 *   verbatim, so a foreign writer could place arbitrary bytes there).
 *
 * If this breaks, first check:
 *   - the parseRfc3339Zulu guard on the attr_deleted branch of record_to_link.
 */
BOOST_AUTO_TEST_CASE(fail_on_malformed_deleted_attribute)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.is_active = false;
    record.attributes[std::string(attr_deleted)] = "not-a-timestamp";

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(record, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));
}

/**
 * [Unit][App] record_to_link rejects a non-RFC-3339 stats `last` timestamp.
 *
 * Scenario:
 *   Given a record whose lnk_stats `last` field decodes to "not-a-timestamp".
 *   When decoded.
 *   Then it returns false with error_bad_attribute.
 *
 * If this breaks, first check:
 *   - the parseRfc3339Zulu guard on the `last` branch of decode_stats.
 */
BOOST_AUTO_TEST_CASE(fail_on_malformed_stats_last_timestamp)
{
    ::LinkRecord record;
    record.short_code = "abc";
    record.target_url = "https://example.com/";
    record.attributes[std::string(attr_stats)] =
        "total=1&r24=0&r7=0&last=not-a-timestamp";

    Link restored;
    std::string error;
    BOOST_CHECK(!record_to_link(record, &restored, &error));
    BOOST_CHECK_EQUAL(error, std::string(error_bad_attribute));
}
