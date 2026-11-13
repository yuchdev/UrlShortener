#define BOOST_TEST_MODULE CommandsArgSpecValidationTest
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::parse_request;
using url_shortener::commands::validate_args;

namespace
{

// Builds a one-string-arg spec with a given name.
ArgSpec string_arg(std::string name, bool required = false)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    spec.required = required;
    return spec;
}

// Validates @p args_json against @p specs and returns the resulting error
// (code == none on success). The out-view is ignored.
CommandError run(const std::vector<ArgSpec>& specs, std::string_view args_json)
{
    CommandError error;
    validate_args(specs, args_json, nullptr, &error);
    return error;
}

// True when validation of @p args_json against @p specs succeeds.
bool accepts(const std::vector<ArgSpec>& specs, std::string_view args_json)
{
    return validate_args(specs, args_json, nullptr, nullptr);
}

// The reason word (the part after "<name>: ") appears in the message.
bool reason_is(const CommandError& error, std::string_view reason)
{
    return error.message.find(reason) != std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------------------
// One accepting and one rejecting case per ArgType, plus wrong-type matrix.
// ---------------------------------------------------------------------------

// Scenario: each ArgType accepts its canonical value and rejects a value of a
// clearly different JSON kind.
// Boundaries: covers all eight catalog types in one pass.
BOOST_AUTO_TEST_CASE(accept_and_reject_per_type)
{
    // string
    BOOST_TEST(accepts({string_arg("s")}, R"({"s":"x"})"));
    BOOST_TEST(reason_is(run({string_arg("s")}, R"({"s":1})"), "wrong_type"));

    // integer
    ArgSpec n;
    n.name = "n";
    n.type = ArgType::integer;
    BOOST_TEST(accepts({n}, R"({"n":7})"));
    BOOST_TEST(reason_is(run({n}, R"({"n":"7"})"), "wrong_type"));

    // boolean
    ArgSpec b;
    b.name = "b";
    b.type = ArgType::boolean;
    BOOST_TEST(accepts({b}, R"({"b":true})"));
    BOOST_TEST(reason_is(run({b}, R"({"b":1})"), "wrong_type"));

    // enumeration
    ArgSpec e;
    e.name = "e";
    e.type = ArgType::enumeration;
    e.enum_values = {"temporary", "permanent"};
    BOOST_TEST(accepts({e}, R"({"e":"permanent"})"));
    BOOST_TEST(reason_is(run({e}, R"({"e":1})"), "wrong_type"));

    // timestamp
    ArgSpec t;
    t.name = "t";
    t.type = ArgType::timestamp;
    BOOST_TEST(accepts({t}, R"({"t":"2026-01-01T00:00:00Z"})"));
    BOOST_TEST(reason_is(run({t}, R"({"t":1})"), "invalid_timestamp"));

    // string_list
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    BOOST_TEST(accepts({l}, R"({"l":["a","b"]})"));
    BOOST_TEST(reason_is(run({l}, R"({"l":{}})"), "wrong_type"));

    // string_map
    ArgSpec m;
    m.name = "m";
    m.type = ArgType::string_map;
    BOOST_TEST(accepts({m}, R"({"m":{"k":"v"}})"));
    BOOST_TEST(reason_is(run({m}, R"({"m":[]})"), "wrong_type"));

    // object
    ArgSpec o;
    o.name = "o";
    o.type = ArgType::object;
    o.fields = {string_arg("name")};
    BOOST_TEST(accepts({o}, R"({"o":{"name":"z"}})"));
    BOOST_TEST(reason_is(run({o}, R"({"o":[]})"), "wrong_type"));
}

// ---------------------------------------------------------------------------
// Unknown key, missing required, null handling.
// ---------------------------------------------------------------------------

// Scenario: an undeclared top-level key is rejected as unknown and the message
// names the key.
BOOST_AUTO_TEST_CASE(unknown_key_rejected)
{
    const CommandError error = run({string_arg("s")}, R"({"bogus":"x"})");
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(reason_is(error, "unknown"));
    BOOST_TEST(error.message.find("bogus") != std::string::npos);
}

// Scenario: a required arg that is absent is rejected as missing.
BOOST_AUTO_TEST_CASE(missing_required_rejected)
{
    const CommandError error = run({string_arg("s", true)}, R"({})");
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(reason_is(error, "missing"));
}

// Scenario: explicit null is a wrong_type for a non-nullable arg and accepted
// for a nullable one; absence and explicit null are distinguishable.
// Boundaries: has/is_null for absent, null, and present-value.
BOOST_AUTO_TEST_CASE(null_and_nullable_distinction)
{
    ArgSpec nn = string_arg("s");  // not nullable
    BOOST_TEST(reason_is(run({nn}, R"({"s":null})"), "wrong_type"));

    ArgSpec yes = string_arg("s");
    yes.nullable = true;
    BOOST_TEST(accepts({yes}, R"({"s":null})"));

    ArgsView view;
    BOOST_REQUIRE(validate_args({yes}, R"({"s":null})", &view, nullptr));
    BOOST_TEST(view.has("s"));
    BOOST_TEST(view.is_null("s"));
    BOOST_TEST(!view.has("absent"));
    BOOST_TEST(!view.is_null("absent"));
    BOOST_TEST(!view.get_string("s").has_value());

    ArgsView view2;
    BOOST_REQUIRE(validate_args({yes}, R"({"s":"v"})", &view2, nullptr));
    BOOST_TEST(view2.has("s"));
    BOOST_TEST(!view2.is_null("s"));
}

// ---------------------------------------------------------------------------
// Integer bounds and type confusion (threat model T-07).
// ---------------------------------------------------------------------------

// Scenario: integer bounds are inclusive; overflow, float and string forms are
// all rejected.
// Boundaries: min, max, min-1, max+1, LLONG_MAX+1, 1.0, 1e2, "5".
BOOST_AUTO_TEST_CASE(integer_bounds_and_type_confusion)
{
    ArgSpec n;
    n.name = "n";
    n.type = ArgType::integer;
    n.min = 0;
    n.max = 100;

    BOOST_TEST(accepts({n}, R"({"n":0})"));  // min
    BOOST_TEST(accepts({n}, R"({"n":100})"));  // max
    BOOST_TEST(reason_is(run({n}, R"({"n":-1})"), "out_of_range"));
    BOOST_TEST(reason_is(run({n}, R"({"n":101})"), "out_of_range"));

    ArgSpec free;
    free.name = "n";
    free.type = ArgType::integer;
    BOOST_TEST(
        reason_is(run({free}, R"({"n":9223372036854775808})"), "out_of_range"));
    BOOST_TEST(reason_is(run({free}, R"({"n":1.0})"), "wrong_type"));
    BOOST_TEST(reason_is(run({free}, R"({"n":1e2})"), "wrong_type"));
    BOOST_TEST(reason_is(run({free}, R"({"n":"5"})"), "wrong_type"));

    // The extreme representable bounds parse.
    ArgSpec wide;
    wide.name = "n";
    wide.type = ArgType::integer;
    BOOST_TEST(accepts({wide}, R"({"n":9223372036854775807})"));
    BOOST_TEST(accepts({wide}, R"({"n":-9223372036854775808})"));
}

// ---------------------------------------------------------------------------
// Enumeration and timestamp.
// ---------------------------------------------------------------------------

// Scenario: enum matching is case-sensitive and empty is not a member.
BOOST_AUTO_TEST_CASE(enum_cases)
{
    ArgSpec e;
    e.name = "e";
    e.type = ArgType::enumeration;
    e.enum_values = {"temporary", "permanent"};
    BOOST_TEST(accepts({e}, R"({"e":"temporary"})"));
    BOOST_TEST(reason_is(run({e}, R"({"e":"Temporary"})"), "not_in_enum"));
    BOOST_TEST(reason_is(run({e}, R"({"e":""})"), "not_in_enum"));
}

// Scenario: timestamp validation defers entirely to the catalog-mandated
// url_shortener::parseRfc3339Zulu; a valid Zulu string passes, while a bad
// month, a garbage string and a non-string all fail as invalid_timestamp.
// Note: parseRfc3339Zulu is lenient about the trailing 'Z' literal (shared
// core function used by REST today), so a missing-Z string is accepted here;
// see the deviation note. We pin that behavior rather than change core/utils.
BOOST_AUTO_TEST_CASE(timestamp_cases)
{
    ArgSpec t;
    t.name = "t";
    t.type = ArgType::timestamp;
    BOOST_TEST(accepts({t}, R"({"t":"2026-01-01T00:00:00Z"})"));
    BOOST_TEST(reason_is(run({t}, R"({"t":"2026-13-01T00:00:00Z"})"),
                         "invalid_timestamp"));
    BOOST_TEST(
        reason_is(run({t}, R"({"t":"not-a-timestamp"})"), "invalid_timestamp"));
    BOOST_TEST(reason_is(run({t}, R"({"t":5})"), "invalid_timestamp"));
    // Pinned leniency of the mandated validator (missing trailing Z accepted).
    BOOST_TEST(accepts({t}, R"({"t":"2026-01-01T00:00:00"})"));
}

// ---------------------------------------------------------------------------
// List and map caps.
// ---------------------------------------------------------------------------

// Scenario: list honors max_items, max_length and element type.
BOOST_AUTO_TEST_CASE(string_list_caps)
{
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    l.max_items = 2;
    l.max_length = 3;
    BOOST_TEST(accepts({l}, R"({"l":["ab","cd"]})"));
    BOOST_TEST(reason_is(run({l}, R"({"l":["a","b","c"]})"), "too_many_items"));
    BOOST_TEST(reason_is(run({l}, R"({"l":["abcd"]})"), "too_long"));
    BOOST_TEST(reason_is(run({l}, R"({"l":[1]})"), "wrong_type"));
}

// Scenario: map honors max_items, max_length and value type.
BOOST_AUTO_TEST_CASE(string_map_caps)
{
    ArgSpec m;
    m.name = "m";
    m.type = ArgType::string_map;
    m.max_items = 2;
    m.max_length = 3;
    BOOST_TEST(accepts({m}, R"({"m":{"a":"xy"}})"));
    BOOST_TEST(reason_is(run({m}, R"({"m":{"a":"1","b":"2","c":"3"}})"),
                         "too_many_items"));
    BOOST_TEST(reason_is(run({m}, R"({"m":{"a":"wxyz"}})"), "too_long"));
    BOOST_TEST(reason_is(run({m}, R"({"m":{"a":1}})"), "wrong_type"));
    // A duplicate key inside the map is rejected by the scanner.
    BOOST_TEST(reason_is(run({m}, R"({"m":{"a":"1","a":"2"}})"), "wrong_type"));
}

// ---------------------------------------------------------------------------
// Nested object (campaign shape).
// ---------------------------------------------------------------------------

// Scenario: nested object members must be declared; unknown and non-string
// members are rejected.
BOOST_AUTO_TEST_CASE(nested_object)
{
    ArgSpec campaign;
    campaign.name = "campaign";
    campaign.type = ArgType::object;
    campaign.fields = {string_arg("name"), string_arg("source")};

    BOOST_TEST(
        accepts({campaign}, R"({"campaign":{"name":"n","source":"s"}})"));
    const CommandError unknown =
        run({campaign}, R"({"campaign":{"bogus":"x"}})");
    BOOST_TEST(reason_is(unknown, "unknown"));
    BOOST_TEST(unknown.message.find("campaign.bogus") != std::string::npos);
    BOOST_TEST(
        reason_is(run({campaign}, R"({"campaign":{"name":1}})"), "wrong_type"));
}

// ---------------------------------------------------------------------------
// one_of_group.
// ---------------------------------------------------------------------------

// Scenario: exactly one of a one_of group must be present (link.get slug|id).
// Boundaries: none present, one present, two present.
BOOST_AUTO_TEST_CASE(one_of_group)
{
    ArgSpec slug = string_arg("slug");
    slug.one_of_group = "lookup";
    ArgSpec id = string_arg("id");
    id.one_of_group = "lookup";
    const std::vector<ArgSpec> specs = {slug, id};

    BOOST_TEST(reason_is(run(specs, R"({})"), "one_of_violation"));
    BOOST_TEST(accepts(specs, R"({"slug":"x"})"));
    BOOST_TEST(accepts(specs, R"({"id":"y"})"));
    BOOST_TEST(
        reason_is(run(specs, R"({"slug":"x","id":"y"})"), "one_of_violation"));
}

// ---------------------------------------------------------------------------
// Messages never contain argument values (contract C6, threat model T-02).
// ---------------------------------------------------------------------------

// Scenario: a secret-looking value in a wrongly-typed arg never appears in the
// error message.
BOOST_AUTO_TEST_CASE(message_omits_values)
{
    ArgSpec n;
    n.name = "n";
    n.type = ArgType::integer;
    const CommandError error = run({n}, R"({"n":"SUPERSECRETVALUE"})");
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(error.message.find("SUPERSECRETVALUE") == std::string::npos);
    BOOST_TEST(reason_is(error, "wrong_type"));
}

// ---------------------------------------------------------------------------
// ArgsView accessors decode strings (escapes and \u) and return nullopt for
// absent/null.
// ---------------------------------------------------------------------------

// Scenario: typed accessors return decoded values and nullopt for absent args.
// Boundaries: \n escape, \u escape, list/map/object re-reads.
BOOST_AUTO_TEST_CASE(argsview_accessors)
{
    std::vector<ArgSpec> specs;
    specs.push_back(string_arg("s"));
    ArgSpec n;
    n.name = "n";
    n.type = ArgType::integer;
    specs.push_back(n);
    ArgSpec b;
    b.name = "b";
    b.type = ArgType::boolean;
    specs.push_back(b);
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    specs.push_back(l);
    ArgSpec m;
    m.name = "m";
    m.type = ArgType::string_map;
    specs.push_back(m);
    ArgSpec o;
    o.name = "o";
    o.type = ArgType::object;
    o.fields = {string_arg("name")};
    specs.push_back(o);

    const std::string args = R"({"s":"line\nbreak A","n":-42,"b":true,)"
                             R"("l":["x","y"],"m":{"k":"v"},"o":{"name":"z"}})";
    ArgsView view;
    BOOST_REQUIRE(validate_args(specs, args, &view, nullptr));

    BOOST_TEST(view.get_string("s").value() == "line\nbreak A");
    BOOST_TEST(view.get_int("n").value() == -42);
    BOOST_TEST(view.get_bool("b").value() == true);

    const auto list = view.get_string_list("l");
    BOOST_REQUIRE(list.has_value());
    BOOST_REQUIRE_EQUAL(list->size(), 2u);
    BOOST_TEST((*list)[0] == "x");
    BOOST_TEST((*list)[1] == "y");

    const auto map = view.get_string_map("m");
    BOOST_REQUIRE(map.has_value());
    BOOST_TEST(map->at("k") == "v");

    const auto nested = view.get_object("o");
    BOOST_REQUIRE(nested.has_value());
    BOOST_TEST(nested->get_string("name").value() == "z");

    BOOST_TEST(view.raw("n") == "-42");

    // Absent args read as nullopt.
    BOOST_TEST(!view.get_string("absent").has_value());
    BOOST_TEST(!view.get_int("absent").has_value());
    BOOST_TEST(!view.get_bool("absent").has_value());
    BOOST_TEST(!view.get_string_list("absent").has_value());
    BOOST_TEST(!view.get_string_map("absent").has_value());
    BOOST_TEST(!view.get_object("absent").has_value());
    BOOST_TEST(view.raw("absent").empty());
}

// ---------------------------------------------------------------------------
// Catalog drift: hand-built link.create / link.update specs validate the
// catalog example bodies (success criterion 2).
// ---------------------------------------------------------------------------

namespace
{

// Spec set mirroring the docs/commands/README.md link.create table, with the
// decision-D10 per-arg caps (65536 bytes / 4096 items).
std::vector<ArgSpec> link_create_specs()
{
    ArgSpec target_url = string_arg("target_url", true);
    target_url.max_length = 65536;
    ArgSpec slug = string_arg("slug");

    ArgSpec redirect_type;
    redirect_type.name = "redirect_type";
    redirect_type.type = ArgType::enumeration;
    redirect_type.enum_values = {"temporary", "permanent"};

    ArgSpec expires_at;
    expires_at.name = "expires_at";
    expires_at.type = ArgType::timestamp;

    ArgSpec enabled;
    enabled.name = "enabled";
    enabled.type = ArgType::boolean;

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;
    tags.max_items = 4096;
    tags.max_length = 65536;

    ArgSpec metadata;
    metadata.name = "metadata";
    metadata.type = ArgType::string_map;
    metadata.max_items = 4096;
    metadata.max_length = 65536;

    ArgSpec campaign;
    campaign.name = "campaign";
    campaign.type = ArgType::object;
    campaign.nullable = true;
    campaign.fields = {string_arg("name"),
                       string_arg("source"),
                       string_arg("medium"),
                       string_arg("term"),
                       string_arg("content"),
                       string_arg("id")};

    return {target_url,
            slug,
            redirect_type,
            expires_at,
            enabled,
            tags,
            metadata,
            campaign};
}

// Spec set mirroring the link.update table.
std::vector<ArgSpec> link_update_specs()
{
    ArgSpec slug = string_arg("slug", true);

    ArgSpec enabled;
    enabled.name = "enabled";
    enabled.type = ArgType::boolean;

    ArgSpec expires_at;
    expires_at.name = "expires_at";
    expires_at.type = ArgType::timestamp;
    expires_at.nullable = true;

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;
    tags.max_items = 4096;
    tags.max_length = 65536;

    ArgSpec metadata;
    metadata.name = "metadata";
    metadata.type = ArgType::string_map;
    metadata.max_items = 4096;
    metadata.max_length = 65536;

    ArgSpec campaign;
    campaign.name = "campaign";
    campaign.type = ArgType::object;
    campaign.nullable = true;
    campaign.fields = {string_arg("name"),
                       string_arg("source"),
                       string_arg("medium"),
                       string_arg("term"),
                       string_arg("content"),
                       string_arg("id")};

    return {slug, enabled, expires_at, tags, metadata, campaign};
}

}  // namespace

// Scenario: the catalog example bodies validate against the hand-built specs,
// so schema/doc drift is caught early.
BOOST_AUTO_TEST_CASE(catalog_example_bodies)
{
    BOOST_TEST(
        accepts(link_create_specs(),
                R"({"target_url":"https://example.com/docs","slug":"docs"})"));
    BOOST_TEST(accepts(
        link_create_specs(),
        R"({"target_url":"https://example.com","redirect_type":"permanent",)"
        R"("enabled":true,"tags":["a","b"],"metadata":{"k":"v"},)"
        R"("campaign":{"name":"launch","source":"news"}})"));
    BOOST_TEST(
        accepts(link_create_specs(),
                R"({"target_url":"https://example.com","campaign":null})"));

    BOOST_TEST(
        accepts(link_update_specs(), R"({"slug":"docs","enabled":false})"));
    BOOST_TEST(accepts(link_update_specs(),
                       R"({"slug":"docs","expires_at":null,"campaign":null})"));
}

// ---------------------------------------------------------------------------
// Per-arg caps do not relax top-level envelope DoS caps (threat model T-06).
// ---------------------------------------------------------------------------

// Scenario: a string_list declaring max_items == 4096 accepts a 4096-item list
// and rejects a 4097-item list; the message names the arg, not element values.
// A 257-member top-level envelope is still rejected by the request-envelope
// scan, confirming the per-arg nested caps do not relax the envelope caps.
BOOST_AUTO_TEST_CASE(per_arg_caps_vs_envelope_caps)
{
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    l.max_items = 4096;
    l.max_length = 8;

    const auto build_list = [](std::size_t count)
    {
        std::string json = R"({"l":[)";
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                json += ',';
            }
            json += "\"x\"";
        }
        json += "]}";
        return json;
    };

    BOOST_TEST(accepts({l}, build_list(4096)));
    const CommandError over = run({l}, build_list(4097));
    BOOST_TEST(reason_is(over, "too_many_items"));
    BOOST_TEST(over.message.find('"') == std::string::npos);  // no values

    // A 257-member top-level envelope object is rejected by parse_request; the
    // scanner's envelope member cap (256) is independent of the per-arg caps.
    std::string envelope = "{";
    for (std::size_t i = 0; i < 257; ++i) {
        if (i != 0) {
            envelope += ',';
        }
        envelope += "\"k" + std::to_string(i) + "\":0";
    }
    envelope += "}";
    const auto parsed = parse_request(envelope);
    BOOST_TEST(!parsed.ok());
    BOOST_TEST((parsed.error.code == CommandErrorCode::invalid_envelope));
}

// Scenario: a string_map with ~4096 distinct keys validates without the
// quadratic duplicate-key scan (carry-forward: scanner dedup is now hashed).
BOOST_AUTO_TEST_CASE(large_map_distinct_keys)
{
    ArgSpec m;
    m.name = "m";
    m.type = ArgType::string_map;
    m.max_items = 4096;
    m.max_length = 16;

    std::string json = R"({"m":{)";
    for (std::size_t i = 0; i < 4096; ++i) {
        if (i != 0) {
            json += ',';
        }
        json += "\"k" + std::to_string(i) + "\":\"v\"";
    }
    json += "}}";
    BOOST_TEST(accepts({m}, json));
}

// ---------------------------------------------------------------------------
// Embedded NUL rejection (threat model T-03).
// ---------------------------------------------------------------------------

// Scenario: a \u0000 in a decoded key is rejected and a \u0000 in a decoded
// string value is rejected; neither message contains the raw NUL or any other
// input byte.
BOOST_AUTO_TEST_CASE(embedded_nul_rejected)
{
    // NUL in a key: the sanctioned scanner rejects it before enumeration, so
    // validate_args surfaces invalid_field with a fixed, value-free message.
    const CommandError key_error = run({string_arg("s")}, R"({"\u0000k":"v"})");
    BOOST_TEST((key_error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(reason_is(key_error, "malformed"));
    BOOST_TEST(key_error.message.find('\0') == std::string::npos);

    // NUL in a string value: rejected as wrong_type, message carries no NUL.
    const CommandError value_error =
        run({string_arg("s")}, R"({"s":"a\u0000b"})");
    BOOST_TEST((value_error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(reason_is(value_error, "wrong_type"));
    BOOST_TEST(value_error.message.find('\0') == std::string::npos);

    // NUL in a list element is also rejected.
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    const CommandError list_error = run({l}, R"({"l":["a\u0000b"]})");
    BOOST_TEST(reason_is(list_error, "wrong_type"));
    BOOST_TEST(list_error.message.find('\0') == std::string::npos);
}

// ---------------------------------------------------------------------------
// Malformed args object.
// ---------------------------------------------------------------------------

// Scenario: args text that is not a JSON object fails as invalid_field with a
// value-free message.
BOOST_AUTO_TEST_CASE(malformed_args_object)
{
    const CommandError error =
        run({string_arg("s")}, R"(["not","an","object"])");
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(error.message.find("args") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Review follow-ups (verify-subtask PARTIAL, security-auditor MEDIUM).
// ---------------------------------------------------------------------------

// Scenario: \u escapes in string values, list elements and map values are
// decoded by the ArgsView accessors (BMP code point and a surrogate pair).
BOOST_AUTO_TEST_CASE(argsview_decodes_unicode_escapes)
{
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    ArgSpec m;
    m.name = "m";
    m.type = ArgType::string_map;
    const std::vector<ArgSpec> specs = {string_arg("s"), l, m};

    const std::string args = R"({"s":"Aé","l":["😀"],"m":{"k":"B"}})";
    ArgsView view;
    BOOST_REQUIRE(validate_args(specs, args, &view, nullptr));

    BOOST_TEST(view.get_string("s").value() == "A\xC3\xA9");
    const auto list = view.get_string_list("l");
    BOOST_REQUIRE(list.has_value());
    BOOST_TEST(list->at(0) == "\xF0\x9F\x98\x80");
    const auto map = view.get_string_map("m");
    BOOST_REQUIRE(map.has_value());
    BOOST_TEST(map->at("k") == "B");
}

// Scenario: validate_args enforces the 64 KiB byte cap itself, so callers that
// build args_json directly (CLI --arg, REST resource routes) without going
// through parse_request still cannot submit an unbounded body.
BOOST_AUTO_TEST_CASE(oversized_args_rejected_without_envelope)
{
    ArgSpec l;
    l.name = "l";
    l.type = ArgType::string_list;
    l.max_items = 4096;
    l.max_length = 65536;

    // Just over 64 KiB: one long string element.
    const std::string big = R"({"l":[")" + std::string(65536, 'x') + R"("]})";
    const CommandError error = run({l}, big);
    BOOST_TEST((error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(reason_is(error, "too_large"));
    BOOST_TEST(error.message.find("xxxx") == std::string::npos);

    // A body comfortably under the cap still validates.
    const std::string ok = R"({"l":[")" + std::string(1000, 'x') + R"("]})";
    BOOST_TEST((run({l}, ok).code == CommandErrorCode::none));
}
