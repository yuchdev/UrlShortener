#define BOOST_TEST_MODULE TuiFormModelTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/form_model.hpp>

/**
 * @file 07_form_model_test.cpp
 * @brief Per-ArgKind behaviour tests for the generic FormModel (FTXUI-free).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: an untouched field is omitted; a set field is encoded per its kind;
 *           a required-but-missing field, an out-of-range enum, a non-numeric or
 *           overflowing integer, an invalid timestamp and an unparseable
 *           raw-object all fail advisory validation; a boolean is tri-state (only
 *           emitted once touched); clearing a non-nullable field is rejected; and
 *           an object encodes only its non-empty members.
 * Boundaries: empty optional -> untouched; required empty -> error; all-empty
 *           object omitted; unknown ArgKind degrades to a string field.
 * On failure first check: `encode_field` / `validate` in `src/tui/form_model.cpp`.
 */

using url_shortener::tui::ArgInfo;
using url_shortener::tui::ArgKind;
using url_shortener::tui::CommandInfo;
using url_shortener::tui::FieldState;
using url_shortener::tui::FormModel;

namespace
{

ArgInfo arg(const std::string& name, ArgKind kind, bool required = false)
{
    ArgInfo info;
    info.name = name;
    info.kind = kind;
    info.required = required;
    return info;
}

CommandInfo one_arg_command(const ArgInfo& a)
{
    CommandInfo info;
    info.name = "demo.run";  // generic, not a real catalog command
    info.args = {a};
    return info;
}

}  // namespace

// ---------------------------------------------------------------------------
// Untouched fields are omitted entirely (default applies server-side).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(untouched_fields_omitted)
{
    CommandInfo info;
    info.name = "demo.run";
    info.args = {arg("a", ArgKind::string), arg("b", ArgKind::integer),
                 arg("c", ArgKind::boolean)};
    const FormModel model(info);
    BOOST_CHECK_EQUAL(model.to_args_json(), "{}");
}

// ---------------------------------------------------------------------------
// Each kind encodes its set value with correct JSON typing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(string_set_encoded)
{
    FormModel model(one_arg_command(arg("s", ArgKind::string)));
    model.set_text(0, "hi");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"s\":\"hi\"}");
}

BOOST_AUTO_TEST_CASE(integer_set_encoded_unquoted)
{
    FormModel model(one_arg_command(arg("n", ArgKind::integer)));
    model.set_text(0, "42");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"n\":42}");
}

BOOST_AUTO_TEST_CASE(boolean_tri_state_only_emitted_when_touched)
{
    FormModel model(one_arg_command(arg("flag", ArgKind::boolean)));
    BOOST_CHECK_EQUAL(model.to_args_json(), "{}");  // untouched: omitted
    model.set_checked(0, false);                    // touched, value false
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"flag\":false}");
    model.set_checked(0, true);
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"flag\":true}");
}

BOOST_AUTO_TEST_CASE(enum_set_emits_selected_string)
{
    ArgInfo a = arg("mode", ArgKind::enumeration);
    a.enum_values = {"temporary", "permanent"};
    FormModel model(one_arg_command(a));
    model.set_choice(0, 1);
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"mode\":\"permanent\"}");
}

BOOST_AUTO_TEST_CASE(string_list_set_encoded_as_array)
{
    FormModel model(one_arg_command(arg("tags", ArgKind::string_list)));
    model.set_text(0, "a, b");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"tags\":[\"a\",\"b\"]}");
}

BOOST_AUTO_TEST_CASE(string_map_set_encoded_as_object)
{
    FormModel model(one_arg_command(arg("meta", ArgKind::string_map)));
    model.set_text(0, "k=v");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"meta\":{\"k\":\"v\"}}");
}

BOOST_AUTO_TEST_CASE(timestamp_passed_through_as_string)
{
    FormModel model(one_arg_command(arg("expires_at", ArgKind::timestamp)));
    model.set_text(0, "2030-01-02T03:04:05Z");
    BOOST_CHECK_EQUAL(model.to_args_json(),
                      "{\"expires_at\":\"2030-01-02T03:04:05Z\"}");
}

// ---------------------------------------------------------------------------
// Special bytes are JSON-escaped (quote, backslash, newline, non-ASCII).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(string_value_escaped_for_every_byte)
{
    FormModel model(one_arg_command(arg("s", ArgKind::string)));
    model.set_text(0, std::string("a\"b\\c\nd\x01y"));
    BOOST_CHECK_EQUAL(model.to_args_json(),
                      "{\"s\":\"a\\\"b\\\\c\\nd\\u0001y\"}");
}

// ---------------------------------------------------------------------------
// Validation: required missing, enum out of range, bad integer/timestamp.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(required_missing_fails)
{
    FormModel model(one_arg_command(arg("s", ArgKind::string, true)));
    BOOST_CHECK(!model.validate());
    BOOST_REQUIRE(model.fields()[0].error.has_value());
    BOOST_CHECK_EQUAL(*model.fields()[0].error, "required");
}

BOOST_AUTO_TEST_CASE(enum_out_of_range_fails)
{
    ArgInfo a = arg("mode", ArgKind::enumeration);
    a.enum_values = {"x", "y"};
    FormModel model(one_arg_command(a));
    model.fields()[0].choice = 5;                    // force out of range
    model.fields()[0].state = FieldState::set;
    BOOST_CHECK(!model.validate());
    BOOST_CHECK(model.fields()[0].error.has_value());
}

BOOST_AUTO_TEST_CASE(integer_abc_and_overflow_fail)
{
    FormModel abc(one_arg_command(arg("n", ArgKind::integer)));
    abc.set_text(0, "abc");
    BOOST_CHECK(!abc.validate());

    FormModel overflow(one_arg_command(arg("n", ArgKind::integer)));
    overflow.set_text(0, "99999999999999999999");
    BOOST_CHECK(!overflow.validate());
}

BOOST_AUTO_TEST_CASE(timestamp_invalid_fails)
{
    FormModel model(one_arg_command(arg("expires_at", ArgKind::timestamp)));
    model.set_text(0, "not-a-time");
    BOOST_CHECK(!model.validate());
}

BOOST_AUTO_TEST_CASE(valid_fields_pass_validation)
{
    ArgInfo a = arg("mode", ArgKind::enumeration);
    a.enum_values = {"x", "y"};
    FormModel model(one_arg_command(a));
    model.set_choice(0, 0);
    BOOST_CHECK(model.validate());
    BOOST_CHECK(!model.fields()[0].error.has_value());
}

// ---------------------------------------------------------------------------
// clear_field is nullable-only.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(clear_field_rejected_on_non_nullable)
{
    FormModel model(one_arg_command(arg("s", ArgKind::string)));
    BOOST_CHECK(!model.clear_field(0));
    BOOST_CHECK(model.fields()[0].state == FieldState::untouched);
}

BOOST_AUTO_TEST_CASE(clear_field_accepted_on_nullable_emits_null)
{
    ArgInfo a = arg("expires_at", ArgKind::timestamp);
    a.nullable = true;
    FormModel model(one_arg_command(a));
    BOOST_CHECK(model.clear_field(0));
    BOOST_CHECK(model.fields()[0].state == FieldState::cleared);
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"expires_at\":null}");
}

// ---------------------------------------------------------------------------
// Object fields: member-based nested JSON vs raw-JSON text field.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(object_with_members_nested_json)
{
    ArgInfo a = arg("campaign", ArgKind::object);
    a.members = {"name", "source", "medium"};
    FormModel model(one_arg_command(a));
    model.set_member(0, 0, "spring");  // name
    model.set_member(0, 2, "email");   // medium (source left empty)
    BOOST_CHECK_EQUAL(
        model.to_args_json(),
        "{\"campaign\":{\"name\":\"spring\",\"medium\":\"email\"}}");
}

BOOST_AUTO_TEST_CASE(object_with_all_empty_members_omitted)
{
    ArgInfo a = arg("campaign", ArgKind::object);
    a.members = {"name", "source"};
    FormModel model(one_arg_command(a));
    model.set_member(0, 0, "x");
    model.set_member(0, 0, "");  // clear it again -> all empty
    BOOST_CHECK_EQUAL(model.to_args_json(), "{}");
}

BOOST_AUTO_TEST_CASE(object_without_members_accepts_only_valid_object)
{
    FormModel good(one_arg_command(arg("raw", ArgKind::object)));
    good.set_text(0, "{\"a\":1}");
    BOOST_CHECK(good.validate());
    BOOST_CHECK_EQUAL(good.to_args_json(), "{\"raw\":{\"a\":1}}");

    FormModel bad(one_arg_command(arg("raw", ArgKind::object)));
    bad.set_text(0, "not json");
    BOOST_CHECK(!bad.validate());
    BOOST_CHECK_EQUAL(bad.to_args_json(), "{}");  // non-object omitted
}

// ---------------------------------------------------------------------------
// Unknown/future ArgKind degrades to a string field (never crashes).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unknown_argkind_degrades_to_string)
{
    ArgInfo a = arg("future", static_cast<ArgKind>(999));
    FormModel model(one_arg_command(a));
    model.set_text(0, "value");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"future\":\"value\"}");
}

// ---------------------------------------------------------------------------
// validation_summary lists only touched fields.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validation_summary_lists_touched_fields)
{
    CommandInfo info;
    info.name = "demo.run";
    info.args = {arg("target_url", ArgKind::string),
                 arg("slug", ArgKind::string)};
    FormModel model(info);
    model.set_text(0, "https://x");
    model.set_text(1, "s");
    model.validate();
    BOOST_CHECK_EQUAL(model.validation_summary(), "target_url ok - slug ok");
}

// ---------------------------------------------------------------------------
// Edit prefill locks a required identity field and leaves others untouched.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(prefill_locks_required_leaves_others_untouched)
{
    CommandInfo info;
    info.name = "demo.update";
    info.args = {arg("slug", ArgKind::string, true),
                 arg("tags", ArgKind::string_list)};
    FormModel model(info);
    model.prefill_from_result("{\"slug\":\"promo\",\"tags\":[\"a\",\"b\"]}");

    BOOST_CHECK(model.fields()[0].locked);
    BOOST_CHECK(model.fields()[0].state == FieldState::set);
    BOOST_CHECK_EQUAL(model.fields()[0].text, "promo");
    // tags prefilled into the buffer but left untouched (not re-sent).
    BOOST_CHECK(model.fields()[1].state == FieldState::untouched);
    BOOST_CHECK_EQUAL(model.fields()[1].text, "a, b");
    BOOST_CHECK_EQUAL(model.to_args_json(), "{\"slug\":\"promo\"}");

    // A locked field ignores edits.
    model.set_text(0, "other");
    BOOST_CHECK_EQUAL(model.fields()[0].text, "promo");
}
