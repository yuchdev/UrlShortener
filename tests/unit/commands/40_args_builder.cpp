#define BOOST_TEST_MODULE CommandsArgsBuilderTest
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/args_builder.hpp"
#include "url_shortener/commands/json_scan.hpp"

using url_shortener::commands::args_builder;
using url_shortener::commands::decode_json_string;
using url_shortener::commands::find_member;
using url_shortener::commands::json_kind;
using url_shortener::commands::json_member;
using url_shortener::commands::scan_object;
using url_shortener::commands::scan_result;

namespace
{

/// Scans @p builder output with the 01.0 scanner, asserting it is exactly one
/// well-formed JSON object, and returns the parsed result. This is how every
/// case proves the builder emits text the real ingress parser accepts.
scan_result parse(const args_builder& b)
{
    scan_result r = scan_object(b.str());
    BOOST_REQUIRE_MESSAGE(r.ok,
                          "scan_object rejected builder output '" << b.str()
                                                                  << "': "
                                                                  << r.error);
    return r;
}

/// Decodes the string value of member @p name in @p r, requiring it to be a
/// JSON string, and returns the decoded UTF-8.
std::string decoded_string(const scan_result& r, std::string_view name)
{
    const json_member* m = find_member(r.members, name);
    BOOST_REQUIRE_MESSAGE(m != nullptr, "member not found: " << name);
    BOOST_REQUIRE((m->kind == json_kind::string));
    auto decoded = decode_json_string(m->raw);
    BOOST_REQUIRE_MESSAGE(decoded.has_value(),
                          "value of '" << name << "' did not decode");
    return *decoded;
}

/// The byte sequence of U+FFFD (the Unicode replacement character), which
/// `jsonEscape` substitutes for each malformed UTF-8 byte.
const std::string k_replacement = "\xEF\xBF\xBD";

}  // namespace

// ---------------------------------------------------------------------------
// Empty and basic shape
// ---------------------------------------------------------------------------

// Type: unit (args JSON assembly - shape).
// Scenario: a fresh builder is empty and serializes to the canonical empty
//   object "{}" that scan_object accepts with zero members.
// Boundaries: empty() true before any add; str() is exactly "{}".
// On failure first check: did str() special-case the empty vector?

BOOST_AUTO_TEST_CASE(empty_builder_serializes_to_empty_object)
{
    args_builder b;
    BOOST_TEST(b.empty());
    BOOST_TEST(b.str() == "{}");

    scan_result r = parse(b);
    BOOST_TEST(r.members.empty());
}

// Type: unit (args JSON assembly - every typed adder once).
// Scenario: one member of each kind is added; the object round-trips through
//   scan_object with the right kinds and values.
// Boundaries: add_string, add_bool, add_int, add_null, add_raw(object),
//   add_string_list, add_string_map in one object.
// On failure first check: which adder serialized the wrong JSON token?

BOOST_AUTO_TEST_CASE(each_adder_once_round_trips)
{
    args_builder b;
    b.add_string("s", "hello")
        .add_bool("flag", true)
        .add_int("n", -42)
        .add_null("nothing")
        .add_raw("obj", R"({"x":1})")
        .add_string_list("list", {"a", "b"})
        .add_string_map("map", {{"k", "v"}});

    scan_result r = parse(b);
    BOOST_REQUIRE_EQUAL(r.members.size(), 7u);

    BOOST_TEST(decoded_string(r, "s") == "hello");

    const json_member* flag = find_member(r.members, "flag");
    BOOST_REQUIRE(flag != nullptr);
    BOOST_TEST((flag->kind == json_kind::boolean));
    BOOST_TEST(flag->raw == "true");

    const json_member* n = find_member(r.members, "n");
    BOOST_REQUIRE(n != nullptr);
    BOOST_TEST((n->kind == json_kind::number));
    BOOST_TEST(n->raw == "-42");

    const json_member* nothing = find_member(r.members, "nothing");
    BOOST_REQUIRE(nothing != nullptr);
    BOOST_TEST((nothing->kind == json_kind::null));

    const json_member* obj = find_member(r.members, "obj");
    BOOST_REQUIRE(obj != nullptr);
    BOOST_TEST((obj->kind == json_kind::object));
    BOOST_TEST(obj->raw == R"({"x":1})");

    const json_member* list = find_member(r.members, "list");
    BOOST_REQUIRE(list != nullptr);
    BOOST_TEST((list->kind == json_kind::array));

    const json_member* map = find_member(r.members, "map");
    BOOST_REQUIRE(map != nullptr);
    BOOST_TEST((map->kind == json_kind::object));
}

// Type: unit (args JSON assembly - ordering).
// Scenario: members are emitted in insertion order (not sorted).
// Boundaries: a reverse-alphabetical insertion sequence keeps its order.
// On failure first check: does str() iterate m_members in insertion order?

BOOST_AUTO_TEST_CASE(insertion_order_is_preserved)
{
    args_builder b;
    b.add_int("z", 1).add_int("m", 2).add_int("a", 3);

    scan_result r = parse(b);
    BOOST_REQUIRE_EQUAL(r.members.size(), 3u);
    BOOST_TEST(r.members[0].key == "z");
    BOOST_TEST(r.members[1].key == "m");
    BOOST_TEST(r.members[2].key == "a");
}

// ---------------------------------------------------------------------------
// Hostile string values
// ---------------------------------------------------------------------------

// Type: unit (security - hostile value escaping, threat model T-78).
// Scenario: quotes, backslash, newline, tab, a control char and non-ASCII
//   UTF-8 all survive as string values that scan_object accepts and
//   decode_json_string returns verbatim.
// Boundaries: '"', '\\', '\n', '\t', 0x01, multi-byte UTF-8.
// On failure first check: is every value routed through jsonString before
//   insertion, and does the output still parse as one object?

BOOST_AUTO_TEST_CASE(hostile_string_values_round_trip)
{
    const std::string quote = "a\"b";
    const std::string backslash = "a\\b";
    const std::string newline = "line1\nline2";
    const std::string tab = "a\tb";
    const std::string control = std::string("x") + '\x01' + "y";
    const std::string utf8 = "caf\xC3\xA9 \xE2\x9C\x93";  // "café ✓"

    args_builder b;
    b.add_string("quote", quote)
        .add_string("backslash", backslash)
        .add_string("newline", newline)
        .add_string("tab", tab)
        .add_string("control", control)
        .add_string("utf8", utf8);

    scan_result r = parse(b);
    BOOST_TEST(decoded_string(r, "quote") == quote);
    BOOST_TEST(decoded_string(r, "backslash") == backslash);
    BOOST_TEST(decoded_string(r, "newline") == newline);
    BOOST_TEST(decoded_string(r, "tab") == tab);
    BOOST_TEST(decoded_string(r, "control") == control);
    BOOST_TEST(decoded_string(r, "utf8") == utf8);
}

// Type: unit (security - invalid UTF-8 sanitization).
// Scenario: a value with lone invalid UTF-8 bytes still produces a well-formed
//   object; the shared escaper substitutes U+FFFD, so the round trip yields the
//   sanitized text rather than crashing or emitting broken JSON.
// Boundaries: a lone 0xFF byte and a bare continuation byte.
// On failure first check: jsonEscape's U+FFFD substitution and that scan_object
//   still accepts the output.
BOOST_AUTO_TEST_CASE(invalid_utf8_value_is_sanitized_but_well_formed)
{
    const std::string bad = std::string("a") + '\xFF' + "b" + '\x80' + "c";
    args_builder b;
    b.add_string("bad", bad);

    scan_result r = parse(b);
    const std::string expected =
        "a" + k_replacement + "b" + k_replacement + "c";
    BOOST_TEST(decoded_string(r, "bad") == expected);
}

// Type: unit (args JSON assembly - empty string value).
// Scenario: an empty string value serializes to "" and decodes back to empty.
// Boundaries: zero-length value distinct from add_null.
// On failure first check: jsonString of "" yields two quotes.
BOOST_AUTO_TEST_CASE(empty_string_value_round_trips)
{
    args_builder b;
    b.add_string("e", "");
    scan_result r = parse(b);
    BOOST_TEST(decoded_string(r, "e") == "");
}

// ---------------------------------------------------------------------------
// Lists and maps
// ---------------------------------------------------------------------------

// Type: unit (args JSON assembly - string list).
// Scenario: an empty list emits [] and a list with hostile elements escapes
//   each element so the array parses and every element decodes verbatim.
// Boundaries: empty vector; elements containing quotes/backslash/newline.
// On failure first check: each element routed through jsonString; commas only
//   between elements.
BOOST_AUTO_TEST_CASE(string_list_empty_and_with_escapes)
{
    args_builder empty;
    empty.add_string_list("items", {});
    {
        scan_result r = parse(empty);
        const json_member* m = find_member(r.members, "items");
        BOOST_REQUIRE(m != nullptr);
        BOOST_TEST((m->kind == json_kind::array));
        BOOST_TEST(m->raw == "[]");
    }

    const std::vector<std::string> values = {"plain", "a\"b", "c\\d", "e\nf"};
    args_builder b;
    b.add_string_list("items", values);
    scan_result r = parse(b);
    const json_member* m = find_member(r.members, "items");
    BOOST_REQUIRE(m != nullptr);
    BOOST_REQUIRE((m->kind == json_kind::array));

    auto arr = url_shortener::commands::scan_array(m->raw);
    BOOST_REQUIRE(arr.ok);
    BOOST_REQUIRE_EQUAL(arr.elements.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        BOOST_REQUIRE((arr.elements[i].kind == json_kind::string));
        auto d = decode_json_string(arr.elements[i].raw);
        BOOST_REQUIRE(d.has_value());
        BOOST_TEST(*d == values[i]);
    }
}

// Type: unit (args JSON assembly - string map sorting and hostile keys).
// Scenario: map members are emitted in sorted key order; hostile map keys and
//   values are both escaped so the nested object parses and round-trips.
// Boundaries: keys inserted out of order; a key containing a quote and a
//   control char.
// On failure first check: std::map ordering drives output; both key and value
//   go through jsonString.
BOOST_AUTO_TEST_CASE(string_map_sorted_and_hostile_keys)
{
    const std::string hostile_key = std::string("b\"") + '\x02';
    std::map<std::string, std::string> values = {
        {"zeta", "1"},
        {"alpha", "2"},
        {hostile_key, "v\\x"},
    };

    args_builder b;
    b.add_string_map("meta", values);
    scan_result r = parse(b);
    const json_member* m = find_member(r.members, "meta");
    BOOST_REQUIRE(m != nullptr);
    BOOST_REQUIRE((m->kind == json_kind::object));

    scan_result inner = scan_object(m->raw);
    BOOST_REQUIRE(inner.ok);
    BOOST_REQUIRE_EQUAL(inner.members.size(), 3u);

    // std::map sorts keys; the hostile key "b\"\x02" sorts before "alpha"?
    // "alpha" < "b..." < "zeta" by byte order.
    BOOST_TEST(inner.members[0].key == "alpha");
    BOOST_TEST(inner.members[1].key == hostile_key);
    BOOST_TEST(inner.members[2].key == "zeta");

    BOOST_TEST(decoded_string(inner, "alpha") == "2");
    BOOST_TEST(decoded_string(inner, "zeta") == "1");
    const json_member* hostile = find_member(inner.members, hostile_key);
    BOOST_REQUIRE(hostile != nullptr);
    auto hv = decode_json_string(hostile->raw);
    BOOST_REQUIRE(hv.has_value());
    BOOST_TEST(*hv == "v\\x");
}

// Type: unit (args JSON assembly - raw object token).
// Scenario: add_raw inserts a pre-built nested object verbatim so a caller can
//   compose a campaign-style sub-object.
// Boundaries: the token is an object, not a scalar.
// On failure first check: add_raw does not re-escape or wrap the token.
BOOST_AUTO_TEST_CASE(add_raw_with_object_token)
{
    args_builder inner;
    inner.add_string("source", "newsletter").add_string("medium", "email");

    args_builder outer;
    outer.add_raw("campaign", inner.str());
    scan_result r = parse(outer);
    const json_member* m = find_member(r.members, "campaign");
    BOOST_REQUIRE(m != nullptr);
    BOOST_REQUIRE((m->kind == json_kind::object));

    scan_result nested = scan_object(m->raw);
    BOOST_REQUIRE(nested.ok);
    BOOST_TEST(decoded_string(nested, "source") == "newsletter");
    BOOST_TEST(decoded_string(nested, "medium") == "email");
}

// ---------------------------------------------------------------------------
// has / empty
// ---------------------------------------------------------------------------

// Type: unit (query helpers).
// Scenario: has() reports membership and empty() flips after the first add.
// Boundaries: absent name, present name, empty-after-construction.
// On failure first check: has() compares against stored names by value.
BOOST_AUTO_TEST_CASE(has_and_empty_reflect_state)
{
    args_builder b;
    BOOST_TEST(b.empty());
    BOOST_TEST(!b.has("slug"));

    b.add_string("slug", "abc");
    BOOST_TEST(!b.empty());
    BOOST_TEST(b.has("slug"));
    BOOST_TEST(!b.has("other"));
}

// ---------------------------------------------------------------------------
// Programming-error rejections
// ---------------------------------------------------------------------------

// Type: unit (security - name grammar enforcement, threat model T-78).
// Scenario: a malformed member name is a programming error and throws
//   std::invalid_argument before any text is built.
// Boundaries: "", "Slug" (uppercase), "a-b" (hyphen), "1x" (leading digit),
//   "a b" (space).
// On failure first check: is_valid_name's grammar [a-z][a-z0-9_]*.
BOOST_AUTO_TEST_CASE(bad_names_throw_invalid_argument)
{
    const std::vector<std::string> bad = {"", "Slug", "a-b", "1x", "a b"};
    for (const std::string& name : bad) {
        args_builder b;
        BOOST_CHECK_THROW(b.add_string(name, "v"), std::invalid_argument);
        BOOST_TEST(b.empty());  // nothing was added on failure
    }
}

// Type: unit (duplicate-key rejection).
// Scenario: adding the same name twice is a programming error and throws.
// Boundaries: duplicate across different adders (string then int).
// On failure first check: has() guard in add_raw before emplace.
BOOST_AUTO_TEST_CASE(duplicate_name_throws)
{
    args_builder b;
    b.add_string("slug", "a");
    BOOST_CHECK_THROW(b.add_int("slug", 1), std::invalid_argument);
    // The original member is intact and the object still parses.
    scan_result r = parse(b);
    BOOST_REQUIRE_EQUAL(r.members.size(), 1u);
    BOOST_TEST(decoded_string(r, "slug") == "a");
}

// Type: unit (security - large value, threat model T-68 limits context).
// Scenario: a 64 KiB string value is emitted without truncation and decodes
//   back to the full length.
// Boundaries: 65536-byte value; the builder itself imposes no cap.
// On failure first check: no size limit in the builder; scan_limits default
//   max_bytes must be raised for the parse since the object exceeds 64 KiB.
BOOST_AUTO_TEST_CASE(large_value_builds_without_truncation)
{
    const std::string big(64 * 1024, 'x');
    args_builder b;
    b.add_string("blob", big);

    // The serialized object is larger than the default 64 KiB scan cap, so
    // raise it for this parse; the builder emitted the whole value.
    url_shortener::commands::scan_limits limits;
    limits.max_bytes = 256 * 1024;
    scan_result r = scan_object(b.str(), limits);
    BOOST_REQUIRE(r.ok);
    const json_member* m = find_member(r.members, "blob");
    BOOST_REQUIRE(m != nullptr);
    auto d = decode_json_string(m->raw);
    BOOST_REQUIRE(d.has_value());
    BOOST_TEST(d->size() == big.size());
    BOOST_TEST(*d == big);
}
