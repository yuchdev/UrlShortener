#define BOOST_TEST_MODULE TuiMockSupportHelpersTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/tui/command_client.hpp>

#include "mock_command_client.hpp"
#include "tui_test_support.hpp"

/**
 * @file 22_mock_support_helpers_test.cpp
 * @brief Self-test for the free-function helpers in `tui_test_support.hpp`
 * (Task 07.0 subtask 22), layered over the final 06.0 `MockCommandClient`.
 *
 * Type: unit (model library, links `url_shortener_tui_model`; FTXUI-free).
 * Scenario: the envelope builders (`ok_envelope`/`err_envelope`) round-trip
 *           through `serialize_response`/`parse_response`; the real-registry
 *           `command.list` fixture
 * (`command_list_fixture`/`command_list_infos`) carries the live `link.create`
 * descriptor; the request-text helpers
 *           (`request_text`/`request_texts`/`request_texts_for`) serialize the
 *           mock's recorded `CommandRequest` structs to byte-exact v1 envelope
 *           text; and `expect_no_calls` passes on an untouched mock while a
 * used mock is detectable as non-empty. Boundaries: an unknown wire error-code
 * name fails closed to `internal`; `request_texts_for` filters by exact command
 * name; byte-exact equality against a literal envelope (no wall-clock
 * assertions). On failure first check: `tui_test_support.hpp` (the helper under
 * test), `tui::equivalent_envelope` in `src/tui/equivalent_cmd.cpp` (the
 *           request serializer the text helpers delegate to), or
 *           `composition::build_command_runtime` (the fixture registry source).
 */

namespace cmd = url_shortener::commands;

using url_shortener::tui::CommandInfo;
using url_shortener::tui::testing::command_list_fixture;
using url_shortener::tui::testing::command_list_infos;
using url_shortener::tui::testing::err_envelope;
using url_shortener::tui::testing::expect_no_calls;
using url_shortener::tui::testing::MockCommandClient;
using url_shortener::tui::testing::ok_envelope;
using url_shortener::tui::testing::request_text;
using url_shortener::tui::testing::request_texts;
using url_shortener::tui::testing::request_texts_for;

namespace
{

cmd::CommandRequest make_request(std::string id,
                                 std::string command,
                                 std::string args_json)
{
    cmd::CommandRequest request;
    request.v = 1;
    request.id = std::move(id);
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    return request;
}

}  // namespace

// ---------------------------------------------------------------------------
// ok_envelope / err_envelope build responses that round-trip through the v1
// serializer, and an unknown error-code name fails closed to `internal`.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(envelope_builders_round_trip)
{
    const cmd::CommandResponse ok = ok_envelope("r1", R"({"a":1})");
    BOOST_TEST(ok.ok);
    BOOST_TEST(ok.id == "r1");
    BOOST_TEST(ok.result_json == R"({"a":1})");

    const cmd::parse_response_result ok_back =
        cmd::parse_response(cmd::serialize_response(ok));
    BOOST_REQUIRE(ok_back.response.has_value());
    BOOST_TEST(ok_back.response->ok);
    BOOST_TEST(ok_back.response->id == "r1");
    BOOST_TEST(ok_back.response->result_json == R"({"a":1})");

    const cmd::CommandResponse err = err_envelope("r2", "not_found");
    BOOST_TEST(!err.ok);
    BOOST_TEST((err.error.code == cmd::CommandErrorCode::not_found));

    const cmd::parse_response_result err_back =
        cmd::parse_response(cmd::serialize_response(err));
    BOOST_REQUIRE(err_back.response.has_value());
    BOOST_TEST(!err_back.response->ok);
    BOOST_TEST(
        (err_back.response->error.code == cmd::CommandErrorCode::not_found));

    // Fail-closed: a name that is not a wire code becomes `internal`, never a
    // silent success.
    const cmd::CommandResponse bad = err_envelope("r3", "no_such_code");
    BOOST_TEST(!bad.ok);
    BOOST_TEST((bad.error.code == cmd::CommandErrorCode::internal));
}

// ---------------------------------------------------------------------------
// The real-registry fixture carries the live `link.create` descriptor, in both
// its `command.list` response form and its CommandInfo form.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(command_list_fixture_has_real_link_create)
{
    const cmd::CommandResponse fixture = command_list_fixture();
    BOOST_REQUIRE(fixture.ok);
    BOOST_TEST(fixture.result_json.find("link.create") != std::string::npos);

    const std::vector<CommandInfo> infos = command_list_infos();
    BOOST_TEST(!infos.empty());

    const CommandInfo* create = nullptr;
    for (const CommandInfo& info : infos) {
        if (info.name == "link.create") {
            create = &info;
            break;
        }
    }
    BOOST_REQUIRE(create != nullptr);
    BOOST_TEST(create->mutating);
    BOOST_TEST(create->permission == "write");
    BOOST_TEST(!create->args.empty());
}

// ---------------------------------------------------------------------------
// request_text / request_texts / request_texts_for serialize the recorded
// structs to byte-exact v1 envelope text, in call order, with name filtering.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(request_texts_are_byte_exact)
{
    MockCommandClient mock;
    mock.enqueue("link.create", ok_envelope("c1", "{}"));
    mock.enqueue("link.list", ok_envelope("c2", "{}"));

    const cmd::CommandRequest create =
        make_request("c1", "link.create", R"({"slug":"a"})");
    const cmd::CommandRequest list = make_request("c2", "link.list", "{}");
    mock.call(create);
    mock.call(list);

    const std::string create_text =
        R"({"v":1,"id":"c1","command":"link.create","args":{"slug":"a"}})";
    const std::string list_text =
        R"({"v":1,"id":"c2","command":"link.list","args":{}})";

    BOOST_TEST(request_text(create) == create_text);

    const std::vector<std::string> all = request_texts(mock);
    BOOST_REQUIRE_EQUAL(all.size(), 2u);
    BOOST_TEST(all[0] == create_text);
    BOOST_TEST(all[1] == list_text);

    const std::vector<std::string> only_create =
        request_texts_for(mock, "link.create");
    BOOST_REQUIRE_EQUAL(only_create.size(), 1u);
    BOOST_TEST(only_create[0] == create_text);

    BOOST_TEST(request_texts_for(mock, "link.delete").empty());
}

// ---------------------------------------------------------------------------
// expect_no_calls passes on an untouched mock; a used mock is detectable as
// non-empty (the condition expect_no_calls would fail on).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(expect_no_calls_detects_usage)
{
    MockCommandClient mock;
    expect_no_calls(mock);  // must not raise on an empty mock
    BOOST_TEST(mock.recorded_requests().empty());

    mock.enqueue("link.list", ok_envelope("x", "{}"));
    mock.call(make_request("x", "link.list", "{}"));

    // expect_no_calls would now fail; assert the underlying detection directly
    // so this self-test stays green while still exercising the "used" path.
    BOOST_TEST(!mock.recorded_requests().empty());
    BOOST_REQUIRE_EQUAL(request_texts(mock).size(), 1u);
}
