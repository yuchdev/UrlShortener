/**
 * @file 06_cli_arg_coercion.cpp
 * @brief Unit tests for the CLI `cmd --arg <name>=<value>` coercer
 * (`url_shortener::cli::coerce_args` / `split_arg_pair`).
 *
 * Scenario: the coercer turns the shell-friendly repeated `--arg name=value`
 * form into the typed `args` JSON object a command expects, using only the
 * command's `ArgSpec` list as the schema; it chooses the JSON *type* and never
 * validates domain rules (the registry does). Every produced object is re-scanned
 * with the 01.0 `scan_object` to prove it is text the real ingress parser accepts.
 *
 * Boundaries: integer tokens follow the shared REST rule `^[0-9]{1,18}$` (leading
 * zeros stripped, base 10, never octal, no sign); booleans accept only
 * `true`/`false`; `string_list` repeats a name and is kept literal; dotted names
 * build `string_map`/`object` members; a `nullable` arg with `null` clears;
 * unknown names and wrong-typed scalars fall back to JSON strings so the registry
 * reports them. Error strings are fixed and never echo the supplied value.
 *
 * On failure first check: whether `coerce_args` grouped the pairs by name in
 * first-seen order and whether the chosen JSON type matches the arg's `ArgType`;
 * a `scan_object` rejection means the emitted text is malformed JSON.
 *
 * Milestone 0006, Task 04.0, subtask 11 (threat model T-77/T-78).
 */
#define BOOST_TEST_MODULE CliArgCoercion
#include <string>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/cli/arg_coercion.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>

using url_shortener::cli::arg_pair;
using url_shortener::cli::coerce_args;
using url_shortener::cli::split_arg_pair;
using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CommandError;
using url_shortener::commands::decode_json_string;
using url_shortener::commands::find_member;
using url_shortener::commands::json_kind;
using url_shortener::commands::json_member;
using url_shortener::commands::scan_object;
using url_shortener::commands::scan_result;
using url_shortener::commands::validate_args;

namespace
{

/// Builds a one-entry pair list for a single `--arg name=value`.
std::vector<arg_pair> one(std::string name, std::string value)
{
    return {arg_pair{std::move(name), std::move(value)}};
}

/// Coerces @p pairs against @p specs, requiring success, and returns the text.
std::string coerce_ok(const std::vector<ArgSpec>& specs,
                      const std::vector<arg_pair>& pairs)
{
    std::string json;
    std::string error;
    const bool ok = coerce_args(specs, pairs, &json, &error);
    BOOST_REQUIRE_MESSAGE(ok, "coerce_args failed unexpectedly: " << error);
    scan_result scan = scan_object(json);
    BOOST_REQUIRE_MESSAGE(scan.ok,
                          "scan_object rejected '" << json << "': "
                                                   << scan.error);
    return json;
}

/// Coerces @p pairs expecting failure, and returns the fixed error string.
std::string coerce_err(const std::vector<ArgSpec>& specs,
                       const std::vector<arg_pair>& pairs)
{
    std::string json = "UNSET";
    std::string error;
    const bool ok = coerce_args(specs, pairs, &json, &error);
    BOOST_REQUIRE_MESSAGE(!ok, "coerce_args succeeded but failure expected");
    BOOST_CHECK_EQUAL(json, "UNSET");  // args_json untouched on failure
    return error;
}

/// The raw JSON token of member @p name, or the empty string when absent.
std::string raw_of(const std::string& json, std::string_view name)
{
    scan_result scan = scan_object(json);
    BOOST_REQUIRE(scan.ok);
    const json_member* m = find_member(scan.members, name);
    return m != nullptr ? m->raw : std::string {};
}

/// The classified kind of member @p name; requires the member to exist.
json_kind kind_of(const std::string& json, std::string_view name)
{
    scan_result scan = scan_object(json);
    BOOST_REQUIRE(scan.ok);
    const json_member* m = find_member(scan.members, name);
    BOOST_REQUIRE_MESSAGE(m != nullptr, "member missing: " << name);
    return m->kind;
}

/// A single typed spec, used to coerce one scalar arg.
ArgSpec scalar_spec(std::string name, ArgType type, bool nullable = false)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = type;
    spec.nullable = nullable;
    if (type == ArgType::enumeration) {
        spec.enum_values = {"temporary", "permanent"};
    }
    return spec;
}

/// Rebuilds the real `link.create` argument schema (catalog 3.1) so the success
/// criteria exercise the exact types the registry validates against.
std::vector<ArgSpec> link_create_specs()
{
    ArgSpec campaign = scalar_spec("campaign", ArgType::object, true);
    for (const char* f :
         {"name", "source", "medium", "term", "content", "id"}) {
        campaign.fields.push_back(scalar_spec(f, ArgType::string));
    }
    return {scalar_spec("target_url", ArgType::string),
            scalar_spec("slug", ArgType::string),
            scalar_spec("redirect_type", ArgType::enumeration),
            scalar_spec("expires_at", ArgType::timestamp),
            scalar_spec("enabled", ArgType::boolean),
            scalar_spec("tags", ArgType::string_list),
            scalar_spec("metadata", ArgType::string_map),
            std::move(campaign)};
}

}  // namespace

// ---------------------------------------------------------------------------
// split_arg_pair
// ---------------------------------------------------------------------------

// Type: unit (token split). Boundary: first '=' only.
BOOST_AUTO_TEST_CASE(split_at_first_equals)
{
    arg_pair out;
    BOOST_REQUIRE(split_arg_pair("limit=10", &out));
    BOOST_CHECK_EQUAL(out.name, "limit");
    BOOST_CHECK_EQUAL(out.value, "10");

    BOOST_REQUIRE(split_arg_pair("a=b=c", &out));
    BOOST_CHECK_EQUAL(out.name, "a");
    BOOST_CHECK_EQUAL(out.value, "b=c");

    BOOST_REQUIRE(split_arg_pair("flag=", &out));
    BOOST_CHECK_EQUAL(out.name, "flag");
    BOOST_CHECK_EQUAL(out.value, "");
}

// Type: unit (invalid input). A token lacking '=' -> the caller maps to bad_pair.
BOOST_AUTO_TEST_CASE(split_rejects_missing_equals_and_empty_name)
{
    arg_pair out;
    BOOST_CHECK(!split_arg_pair("noequals", &out));
    BOOST_CHECK(!split_arg_pair("=x", &out));
    BOOST_CHECK(!split_arg_pair("", &out));
}

// ---------------------------------------------------------------------------
// Scalars: one valid value per ArgType
// ---------------------------------------------------------------------------

// Type: unit (normal). Each ArgType coerces its canonical value.
BOOST_AUTO_TEST_CASE(scalar_valid_values_per_type)
{
    const auto specs = link_create_specs();

    const std::string s = coerce_ok(specs, one("slug", "a"));
    BOOST_CHECK_EQUAL(raw_of(s, "slug"), "\"a\"");

    const std::string i = coerce_ok({scalar_spec("limit", ArgType::integer)},
                                    one("limit", "10"));
    BOOST_CHECK(kind_of(i, "limit") == json_kind::number);
    BOOST_CHECK_EQUAL(raw_of(i, "limit"), "10");

    const std::string b = coerce_ok(specs, one("enabled", "true"));
    BOOST_CHECK(kind_of(b, "enabled") == json_kind::boolean);
    BOOST_CHECK_EQUAL(raw_of(b, "enabled"), "true");

    const std::string e = coerce_ok(specs, one("redirect_type", "permanent"));
    BOOST_CHECK(kind_of(e, "redirect_type") == json_kind::string);

    const std::string t =
        coerce_ok(specs, one("expires_at", "2030-01-01T00:00:00Z"));
    BOOST_CHECK(kind_of(t, "expires_at") == json_kind::string);
}

// ---------------------------------------------------------------------------
// Integer edge cases
// ---------------------------------------------------------------------------

// Type: unit (boundary). Only ^[0-9]{1,18}$ is a number; all else is a string.
BOOST_AUTO_TEST_CASE(integer_edge_cases)
{
    const std::vector<ArgSpec> specs = {scalar_spec("n", ArgType::integer)};

    // Valid: zero, canonicalized leading zeros, max 18 digits.
    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("n", "0")), "n"), "0");
    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("n", "007")), "n"), "7");
    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("n", "00")), "n"), "0");
    BOOST_CHECK(kind_of(coerce_ok(specs, one("n", "123456789012345678")), "n")
                == json_kind::number);

    // Rejected -> emitted as JSON string so the registry reports wrong_type.
    for (const char* bad :
         {"-1", "+1", "1.5", "1234567890123456789", "", "12a", " 5"}) {
        const std::string json = coerce_ok(specs, one("n", bad));
        BOOST_CHECK_MESSAGE(kind_of(json, "n") == json_kind::string,
                            "expected string fallback for '" << bad << "'");
    }
}

// ---------------------------------------------------------------------------
// Boolean edge cases
// ---------------------------------------------------------------------------

// Type: unit (boundary). Only exact true/false; True and 1 fall back to string.
BOOST_AUTO_TEST_CASE(boolean_edge_cases)
{
    const std::vector<ArgSpec> specs = {scalar_spec("b", ArgType::boolean)};
    BOOST_CHECK(kind_of(coerce_ok(specs, one("b", "false")), "b")
                == json_kind::boolean);
    for (const char* bad : {"True", "FALSE", "1", "0", "yes"}) {
        BOOST_CHECK_MESSAGE(kind_of(coerce_ok(specs, one("b", bad)), "b")
                                == json_kind::string,
                            "expected string fallback for '" << bad << "'");
    }
}

// ---------------------------------------------------------------------------
// string_list
// ---------------------------------------------------------------------------

// Type: unit (normal). Repeating the name builds a JSON array in order.
BOOST_AUTO_TEST_CASE(string_list_repeated_single_and_literal)
{
    const std::vector<ArgSpec> specs = {scalar_spec("tags", ArgType::string_list)};

    const std::string many =
        coerce_ok(specs, {arg_pair{"tags", "a"}, arg_pair{"tags", "b"}});
    BOOST_CHECK_EQUAL(raw_of(many, "tags"), "[\"a\",\"b\"]");

    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("tags", "x")), "tags"),
                      "[\"x\"]");

    // Empty value -> a list of one empty string (not an empty list).
    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("tags", "")), "tags"),
                      "[\"\"]");

    // Commas are kept literal (not split into multiple elements).
    BOOST_CHECK_EQUAL(raw_of(coerce_ok(specs, one("tags", "a,b")), "tags"),
                      "[\"a,b\"]");
}

// ---------------------------------------------------------------------------
// Maps and objects via dotted names
// ---------------------------------------------------------------------------

// Type: unit (normal). Dotted names build string_map and object members.
BOOST_AUTO_TEST_CASE(dotted_map_and_object)
{
    const auto specs = link_create_specs();

    const std::string m =
        coerce_ok(specs, {arg_pair{"metadata.utm", "email"},
                          arg_pair{"metadata.src", "newsletter"}});
    // std::map sorts keys, so src precedes utm.
    BOOST_CHECK_EQUAL(raw_of(m, "metadata"),
                      "{\"src\":\"newsletter\",\"utm\":\"email\"}");

    const std::string o = coerce_ok(specs, one("campaign.name", "spring"));
    BOOST_CHECK_EQUAL(raw_of(o, "campaign"), "{\"name\":\"spring\"}");
}

// Type: unit (invalid input). The same dotted key twice -> map_entry_duplicate.
BOOST_AUTO_TEST_CASE(dotted_duplicate_and_empty_key)
{
    const auto specs = link_create_specs();
    BOOST_CHECK_EQUAL(
        coerce_err(specs, {arg_pair{"metadata.k", "1"}, arg_pair{"metadata.k",
                                                                  "2"}}),
        "map_entry_duplicate");
    BOOST_CHECK_EQUAL(coerce_err(specs, one("metadata.", "v")),
                      "map_entry_empty");
}

// Type: unit (invalid input). A dotted prefix that is not a map/object arg is
// rejected (bad_name): it cannot be a literal top-level key via args_builder.
BOOST_AUTO_TEST_CASE(dotted_prefix_not_a_container)
{
    const auto specs = link_create_specs();
    BOOST_CHECK_EQUAL(coerce_err(specs, one("slug.x", "y")), "bad_name");
    BOOST_CHECK_EQUAL(coerce_err(specs, one("unknown.x", "y")), "bad_name");
    BOOST_CHECK_EQUAL(coerce_err({}, one("a.b", "c")), "bad_name");
}

// ---------------------------------------------------------------------------
// null handling
// ---------------------------------------------------------------------------

// Type: unit (boundary). nullable + null -> JSON null; non-nullable -> string.
BOOST_AUTO_TEST_CASE(null_value_by_nullability)
{
    const std::vector<ArgSpec> nullable = {
        scalar_spec("campaign", ArgType::object, /*nullable=*/true)};
    BOOST_CHECK(kind_of(coerce_ok(nullable, one("campaign", "null")), "campaign")
                == json_kind::null);

    const std::vector<ArgSpec> plain = {scalar_spec("slug", ArgType::string)};
    BOOST_CHECK(kind_of(coerce_ok(plain, one("slug", "null")), "slug")
                == json_kind::string);
}

// ---------------------------------------------------------------------------
// Value escaping and round-trips
// ---------------------------------------------------------------------------

// Type: unit (boundary). Hostile value bytes escape and round-trip intact.
BOOST_AUTO_TEST_CASE(value_bytes_round_trip)
{
    const std::vector<ArgSpec> specs = {scalar_spec("slug", ArgType::string)};
    const std::string payload = "a=\"b\\c\n\xC3\xA9";  // =, quote, backslash, NL, UTF-8
    const std::string json = coerce_ok(specs, one("slug", payload));
    const std::string raw = raw_of(json, "slug");
    auto decoded = decode_json_string(raw);
    BOOST_REQUIRE(decoded.has_value());
    BOOST_CHECK_EQUAL(*decoded, payload);
}

// ---------------------------------------------------------------------------
// Name validation and duplicates
// ---------------------------------------------------------------------------

// Type: unit (invalid input). Empty / out-of-grammar names -> bad_name.
BOOST_AUTO_TEST_CASE(bad_names_rejected)
{
    const auto specs = link_create_specs();
    for (const char* bad : {"", "Slug", "a-b", "a b", "1abc", "_x"}) {
        BOOST_CHECK_MESSAGE(coerce_err(specs, one(bad, "1")) == "bad_name",
                            "expected bad_name for '" << bad << "'");
    }
}

// Type: unit (invalid input). A repeated non-list scalar name -> duplicate_name.
BOOST_AUTO_TEST_CASE(duplicate_scalar_name_rejected)
{
    const std::vector<ArgSpec> specs = {scalar_spec("slug", ArgType::string)};
    BOOST_CHECK_EQUAL(
        coerce_err(specs, {arg_pair{"slug", "a"}, arg_pair{"slug", "b"}}),
        "duplicate_name");
}

// ---------------------------------------------------------------------------
// Unknown command / empty specs
// ---------------------------------------------------------------------------

// Type: unit (normal). With no specs every scalar name becomes a JSON string.
BOOST_AUTO_TEST_CASE(empty_specs_emit_strings)
{
    const std::string json =
        coerce_ok({}, {arg_pair{"limit", "10"}, arg_pair{"enabled", "true"}});
    BOOST_CHECK(kind_of(json, "limit") == json_kind::string);
    BOOST_CHECK(kind_of(json, "enabled") == json_kind::string);
}

// Type: unit (boundary). No pairs -> the empty object.
BOOST_AUTO_TEST_CASE(no_pairs_emits_empty_object)
{
    BOOST_CHECK_EQUAL(coerce_ok({}, {}), "{}");
}

// ---------------------------------------------------------------------------
// Totality under hostile names (success criterion 3)
// ---------------------------------------------------------------------------

// Type: unit (regression). 1000 generated names never throw; bad ones fail
// cleanly, good ones coerce - the builder's programming-error throw is
// unreachable (threat model T-78).
BOOST_AUTO_TEST_CASE(no_exception_under_generated_names)
{
    const auto specs = link_create_specs();
    const char alphabet[] = "abZ0_-. =\n\"\\";
    const std::size_t span = sizeof(alphabet) - 1;
    for (int seed = 0; seed < 1000; ++seed) {
        std::string name;
        int x = seed;
        for (int i = 0; i < 3; ++i) {
            name.push_back(alphabet[static_cast<std::size_t>(x) % span]);
            x /= static_cast<int>(span);
        }
        std::string json;
        std::string error;
        BOOST_CHECK_NO_THROW(
            (void)coerce_args(specs, one(name, "v"), &json, &error));
    }
}

// ---------------------------------------------------------------------------
// End-to-end: link.create coercion passes validate_args (success criterion 2)
// ---------------------------------------------------------------------------

// Type: integration-style unit. The coerced link.create object validates.
BOOST_AUTO_TEST_CASE(link_create_coercion_validates)
{
    const auto specs = link_create_specs();
    const std::vector<arg_pair> pairs = {
        arg_pair{"target_url", "https://a.example"}, arg_pair{"slug", "a"},
        arg_pair{"tags", "x"},                       arg_pair{"tags", "y"},
        arg_pair{"metadata.k", "v"},                 arg_pair{"campaign.name", "n"}};

    const std::string json = coerce_ok(specs, pairs);
    ArgsView view;
    CommandError error;
    const bool ok = validate_args(specs, json, &view, &error);
    BOOST_REQUIRE_MESSAGE(ok, "validate_args rejected coerced link.create: "
                                  << error.message);
    BOOST_CHECK_EQUAL(view.get_string("target_url").value_or(""),
                      "https://a.example");
    auto tags = view.get_string_list("tags");
    BOOST_REQUIRE(tags.has_value());
    BOOST_REQUIRE_EQUAL(tags->size(), 2u);
    BOOST_CHECK_EQUAL((*tags)[0], "x");
}
