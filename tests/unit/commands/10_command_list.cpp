#define BOOST_TEST_MODULE CommandsCommandListTest
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/json_scan.hpp"

using url_shortener::commands::arg_type_name;
using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::decode_json_string;
using url_shortener::commands::descriptor_to_json;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::find_member;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::json_element;
using url_shortener::commands::json_kind;
using url_shortener::commands::json_member;
using url_shortener::commands::kind_name;
using url_shortener::commands::Permission;
using url_shortener::commands::permission_name;
using url_shortener::commands::register_builtin_commands;
using url_shortener::commands::scan_array;
using url_shortener::commands::scan_object;

namespace
{

// A trivial, never-failing sync handler used by the fake descriptors.
CommandResult noop_handler(CommandContext&, const ArgsView&)
{
    CommandResult result;
    result.result_json = "{}";
    return result;
}

// A trivial job factory; registered but never invoked by command.list.
JobHandlerFactory noop_job_factory()
{
    return [](const CommandContext&, const ArgsView&)
               -> std::variant<JobBody, CommandError> {
        return JobBody{[](url_shortener::commands::IJobContext&) {
            return JobOutcome{};
        }};
    };
}

// Executes command.list for the given role and args, requiring success, and
// returns the embedded `result_json`.
std::string list_result(const CommandRegistry& registry, ControlSet role,
                        std::string args_json)
{
    CallerContext caller;
    caller.control_set = role;
    caller.principal = "test";
    caller.transport = "test";

    CommandRequest request;
    request.command = "command.list";
    request.args_json = std::move(args_json);

    const CommandResponse response = registry.execute(caller, request);
    BOOST_REQUIRE(response.ok);
    BOOST_REQUIRE(response.error.code == CommandErrorCode::none);
    return response.result_json;
}

// Parses `{"commands":[...]}` and returns the per-descriptor object texts in
// their serialized order.
std::vector<std::string> command_objects(const std::string& result_json)
{
    const url_shortener::commands::scan_result top = scan_object(result_json);
    BOOST_REQUIRE(top.ok);
    const json_member* commands = find_member(top.members, "commands");
    BOOST_REQUIRE(commands != nullptr);
    BOOST_REQUIRE(commands->kind == json_kind::array);

    const url_shortener::commands::array_scan_result arr =
        scan_array(commands->raw);
    BOOST_REQUIRE(arr.ok);

    std::vector<std::string> objects;
    for (const json_element& element : arr.elements)
    {
        BOOST_REQUIRE(element.kind == json_kind::object);
        objects.push_back(element.raw);
    }
    return objects;
}

// Decodes the string value of a member known to be a JSON string.
std::string string_field(const std::vector<json_member>& members,
                         const std::string& key)
{
    const json_member* member = find_member(members, key);
    BOOST_REQUIRE_MESSAGE(member != nullptr, "missing field: " + key);
    BOOST_REQUIRE(member->kind == json_kind::string);
    const std::optional<std::string> decoded = decode_json_string(member->raw);
    BOOST_REQUIRE(decoded.has_value());
    return *decoded;
}

// Returns the raw token ("true"/"false") of a boolean member.
std::string bool_field(const std::vector<json_member>& members,
                       const std::string& key)
{
    const json_member* member = find_member(members, key);
    BOOST_REQUIRE_MESSAGE(member != nullptr, "missing field: " + key);
    BOOST_REQUIRE(member->kind == json_kind::boolean);
    return member->raw;
}

// A fake write/dry_run descriptor with an enum arg (carrying a default) and a
// required string arg, exercising the richest parts of the arg serializer.
CommandDescriptor fake_aaa()
{
    CommandDescriptor descriptor;
    descriptor.name = "aaa.one";
    descriptor.summary = "first fake";
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = true;
    descriptor.handler = &noop_handler;

    ArgSpec redirect_type;
    redirect_type.name = "redirect_type";
    redirect_type.type = ArgType::enumeration;
    redirect_type.required = false;
    redirect_type.enum_values = {"temporary", "permanent"};
    redirect_type.default_value = "\"temporary\"";
    redirect_type.doc = "redirect kind";

    ArgSpec target_url;
    target_url.name = "target_url";
    target_url.type = ArgType::string;
    target_url.required = true;
    target_url.doc = "url to shorten";

    descriptor.args = {redirect_type, target_url};
    return descriptor;
}

// A fake read job descriptor covering every non-string arg type.
CommandDescriptor fake_bbb()
{
    CommandDescriptor descriptor;
    descriptor.name = "bbb.two";
    descriptor.summary = "second fake";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::job;
    descriptor.mutating = false;
    descriptor.job_factory = noop_job_factory();

    ArgSpec count;
    count.name = "count";
    count.type = ArgType::integer;
    count.doc = "n";

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;
    tags.doc = "tags";

    ArgSpec meta;
    meta.name = "meta";
    meta.type = ArgType::string_map;
    meta.doc = "meta";

    ArgSpec when;
    when.name = "when";
    when.type = ArgType::timestamp;
    when.doc = "when";

    ArgSpec obj;
    obj.name = "obj";
    obj.type = ArgType::object;
    obj.doc = "obj";

    ArgSpec flag;
    flag.name = "flag";
    flag.type = ArgType::boolean;
    flag.required = true;
    flag.doc = "flag";

    descriptor.args = {count, tags, meta, when, obj, flag};
    return descriptor;
}

// A fake read sync descriptor with no args, sorting last.
CommandDescriptor fake_zzz()
{
    CommandDescriptor descriptor;
    descriptor.name = "zzz.three";
    descriptor.summary = "third fake";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.handler = &noop_handler;
    return descriptor;
}

}  // namespace

// ---------------------------------------------------------------------------
// A registry holding only the built-in lists exactly command.list, read/sync/
// non-mutating, with no args.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(builtin_only_lists_command_list)
{
    CommandRegistry registry;
    register_builtin_commands(registry);

    const std::string result = list_result(registry, ControlSet::Admin, "{}");
    const std::vector<std::string> objects = command_objects(result);
    BOOST_REQUIRE_EQUAL(objects.size(), 1u);

    const url_shortener::commands::scan_result obj = scan_object(objects[0]);
    BOOST_REQUIRE(obj.ok);
    BOOST_TEST(string_field(obj.members, "name") == "command.list");
    BOOST_TEST(string_field(obj.members, "summary") ==
               "List all commands and their argument schemas");
    BOOST_TEST(string_field(obj.members, "perm") == "read");
    BOOST_TEST(string_field(obj.members, "kind") == "sync");
    BOOST_TEST(bool_field(obj.members, "mutating") == "false");
    BOOST_TEST(bool_field(obj.members, "dry_run") == "false");

    const json_member* args = find_member(obj.members, "args");
    BOOST_REQUIRE(args != nullptr);
    BOOST_REQUIRE(args->kind == json_kind::array);
    const url_shortener::commands::array_scan_result arr = scan_array(args->raw);
    BOOST_REQUIRE(arr.ok);
    BOOST_TEST(arr.elements.empty());
}

// ---------------------------------------------------------------------------
// With three fakes added, the result equals the descriptor set: name order,
// perm/kind/mutating/dry_run, and every serialized ArgSpec field.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(result_equals_descriptor_set)
{
    CommandRegistry registry;
    register_builtin_commands(registry);
    registry.add(fake_aaa());
    registry.add(fake_bbb());
    registry.add(fake_zzz());

    const std::string result = list_result(registry, ControlSet::Admin, "{}");
    const std::vector<std::string> objects = command_objects(result);
    BOOST_REQUIRE_EQUAL(objects.size(), 4u);

    // Name-sorted order, interleaving the fakes around the built-in.
    std::vector<std::string> names;
    std::vector<url_shortener::commands::scan_result> parsed;
    for (const std::string& text : objects)
    {
        const url_shortener::commands::scan_result obj = scan_object(text);
        BOOST_REQUIRE(obj.ok);
        names.push_back(string_field(obj.members, "name"));
        parsed.push_back(obj);
    }
    BOOST_TEST(names[0] == "aaa.one");
    BOOST_TEST(names[1] == "bbb.two");
    BOOST_TEST(names[2] == "command.list");
    BOOST_TEST(names[3] == "zzz.three");

    // aaa.one: write / sync / mutating / dry_run, two args.
    const std::vector<json_member>& a = parsed[0].members;
    BOOST_TEST(string_field(a, "perm") == "write");
    BOOST_TEST(string_field(a, "kind") == "sync");
    BOOST_TEST(bool_field(a, "mutating") == "true");
    BOOST_TEST(bool_field(a, "dry_run") == "true");

    const json_member* a_args = find_member(a, "args");
    BOOST_REQUIRE(a_args != nullptr);
    const url_shortener::commands::array_scan_result a_arr =
        scan_array(a_args->raw);
    BOOST_REQUIRE(a_arr.ok);
    BOOST_REQUIRE_EQUAL(a_arr.elements.size(), 2u);

    // First arg: the enum, in declared order, with its enum values and default.
    const url_shortener::commands::scan_result a_arg0 =
        scan_object(a_arr.elements[0].raw);
    BOOST_REQUIRE(a_arg0.ok);
    BOOST_TEST(string_field(a_arg0.members, "name") == "redirect_type");
    BOOST_TEST(string_field(a_arg0.members, "type") == "enum");
    BOOST_TEST(bool_field(a_arg0.members, "required") == "false");
    BOOST_TEST(string_field(a_arg0.members, "doc") == "redirect kind");

    const json_member* enum_member = find_member(a_arg0.members, "enum");
    BOOST_REQUIRE(enum_member != nullptr);
    BOOST_REQUIRE(enum_member->kind == json_kind::array);
    const url_shortener::commands::array_scan_result enum_arr =
        scan_array(enum_member->raw);
    BOOST_REQUIRE(enum_arr.ok);
    BOOST_REQUIRE_EQUAL(enum_arr.elements.size(), 2u);
    BOOST_TEST(decode_json_string(enum_arr.elements[0].raw).value() ==
               "temporary");
    BOOST_TEST(decode_json_string(enum_arr.elements[1].raw).value() ==
               "permanent");

    // The default is the verbatim raw token, a JSON string here.
    const json_member* default_member = find_member(a_arg0.members, "default");
    BOOST_REQUIRE(default_member != nullptr);
    BOOST_REQUIRE(default_member->kind == json_kind::string);
    BOOST_TEST(decode_json_string(default_member->raw).value() == "temporary");

    // Second arg: required string, no enum/default.
    const url_shortener::commands::scan_result a_arg1 =
        scan_object(a_arr.elements[1].raw);
    BOOST_REQUIRE(a_arg1.ok);
    BOOST_TEST(string_field(a_arg1.members, "name") == "target_url");
    BOOST_TEST(string_field(a_arg1.members, "type") == "string");
    BOOST_TEST(bool_field(a_arg1.members, "required") == "true");
    BOOST_TEST(find_member(a_arg1.members, "enum") == nullptr);
    BOOST_TEST(find_member(a_arg1.members, "default") == nullptr);

    // bbb.two: read / job / non-mutating, every non-string arg type mapped.
    const std::vector<json_member>& b = parsed[1].members;
    BOOST_TEST(string_field(b, "perm") == "read");
    BOOST_TEST(string_field(b, "kind") == "job");
    BOOST_TEST(bool_field(b, "mutating") == "false");
    BOOST_TEST(bool_field(b, "dry_run") == "false");

    const json_member* b_args = find_member(b, "args");
    BOOST_REQUIRE(b_args != nullptr);
    const url_shortener::commands::array_scan_result b_arr =
        scan_array(b_args->raw);
    BOOST_REQUIRE(b_arr.ok);
    BOOST_REQUIRE_EQUAL(b_arr.elements.size(), 6u);

    const std::vector<std::string> expected_types = {
        "int", "list<string>", "map<string,string>",
        "timestamp", "object", "bool"};
    for (std::size_t i = 0; i < b_arr.elements.size(); ++i)
    {
        const url_shortener::commands::scan_result arg =
            scan_object(b_arr.elements[i].raw);
        BOOST_REQUIRE(arg.ok);
        BOOST_TEST(string_field(arg.members, "type") == expected_types[i]);
    }

    // zzz.three: no args.
    const json_member* z_args = find_member(parsed[3].members, "args");
    BOOST_REQUIRE(z_args != nullptr);
    const url_shortener::commands::array_scan_result z_arr =
        scan_array(z_args->raw);
    BOOST_REQUIRE(z_arr.ok);
    BOOST_TEST(z_arr.elements.empty());
}

// ---------------------------------------------------------------------------
// descriptor_to_json emits a well-formed object; the required flag round-trips
// for both values and an enum arg exposes its values. (Serializer unit test.)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(descriptor_to_json_is_wellformed)
{
    const std::string json = descriptor_to_json(fake_aaa());

    // Well-formed single object with no trailing data.
    const url_shortener::commands::scan_result obj = scan_object(json);
    BOOST_REQUIRE(obj.ok);
    BOOST_TEST(obj.error.empty());

    const json_member* args = find_member(obj.members, "args");
    BOOST_REQUIRE(args != nullptr);
    const url_shortener::commands::array_scan_result arr = scan_array(args->raw);
    BOOST_REQUIRE(arr.ok);
    BOOST_REQUIRE_EQUAL(arr.elements.size(), 2u);

    // required: false then true round-trip to the two literals.
    const url_shortener::commands::scan_result arg0 =
        scan_object(arr.elements[0].raw);
    BOOST_REQUIRE(arg0.ok);
    BOOST_TEST(bool_field(arg0.members, "required") == "false");
    const url_shortener::commands::scan_result arg1 =
        scan_object(arr.elements[1].raw);
    BOOST_REQUIRE(arg1.ok);
    BOOST_TEST(bool_field(arg1.members, "required") == "true");

    // The enum arg exposes its values as a JSON array of strings.
    const json_member* enum_member = find_member(arg0.members, "enum");
    BOOST_REQUIRE(enum_member != nullptr);
    BOOST_REQUIRE(enum_member->kind == json_kind::array);
}

// ---------------------------------------------------------------------------
// A read-only User may call command.list; an unknown extra arg is invalid_field
// because the command takes no args.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(user_may_list_but_unknown_arg_rejected)
{
    CommandRegistry registry;
    register_builtin_commands(registry);

    // User (read) succeeds with no args.
    const std::string result = list_result(registry, ControlSet::User, "{}");
    BOOST_TEST(!command_objects(result).empty());

    // An unknown argument is rejected: the command declares none.
    CallerContext caller;
    caller.control_set = ControlSet::User;
    CommandRequest request;
    request.command = "command.list";
    request.args_json = "{\"foo\":1}";

    const CommandResponse response = registry.execute(caller, request);
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// ---------------------------------------------------------------------------
// Two calls on the same registry return byte-identical output.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(output_is_deterministic)
{
    CommandRegistry registry;
    register_builtin_commands(registry);
    registry.add(fake_aaa());
    registry.add(fake_bbb());
    registry.add(fake_zzz());

    const std::string first = list_result(registry, ControlSet::Admin, "{}");
    const std::string second = list_result(registry, ControlSet::Admin, "{}");
    BOOST_TEST(first == second);
}

// ---------------------------------------------------------------------------
// Every enumerator of each enum has a distinct, non-empty catalog name, and the
// arg-type names follow the catalog vocabulary (not the C++ identifiers).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(name_helpers_are_distinct_and_nonempty)
{
    const std::vector<std::string_view> perms = {
        permission_name(Permission::read),
        permission_name(Permission::write),
        permission_name(Permission::migrate),
        permission_name(Permission::manage_users)};
    for (const std::string_view name : perms)
    {
        BOOST_TEST(!name.empty());
    }
    for (std::size_t i = 0; i < perms.size(); ++i)
    {
        for (std::size_t j = i + 1; j < perms.size(); ++j)
        {
            BOOST_TEST(perms[i] != perms[j]);
        }
    }
    BOOST_TEST(permission_name(Permission::read) == "read");
    BOOST_TEST(permission_name(Permission::manage_users) == "manage_users");

    BOOST_TEST(kind_name(ExecutionKind::sync) == "sync");
    BOOST_TEST(kind_name(ExecutionKind::job) == "job");
    BOOST_TEST(kind_name(ExecutionKind::sync) != kind_name(ExecutionKind::job));

    const std::vector<std::string_view> types = {
        arg_type_name(ArgType::string),
        arg_type_name(ArgType::integer),
        arg_type_name(ArgType::boolean),
        arg_type_name(ArgType::enumeration),
        arg_type_name(ArgType::timestamp),
        arg_type_name(ArgType::string_list),
        arg_type_name(ArgType::string_map),
        arg_type_name(ArgType::object)};
    for (const std::string_view name : types)
    {
        BOOST_TEST(!name.empty());
    }
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        for (std::size_t j = i + 1; j < types.size(); ++j)
        {
            BOOST_TEST(types[i] != types[j]);
        }
    }
    // Catalog vocabulary, not the C++ enumerator names.
    BOOST_TEST(arg_type_name(ArgType::integer) == "int");
    BOOST_TEST(arg_type_name(ArgType::boolean) == "bool");
    BOOST_TEST(arg_type_name(ArgType::enumeration) == "enum");
    BOOST_TEST(arg_type_name(ArgType::string_list) == "list<string>");
    BOOST_TEST(arg_type_name(ArgType::string_map) == "map<string,string>");
}
