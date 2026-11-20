/**
 * @file 00_support_selftest.cpp
 * @brief Boost.Test self-test for the conformance support library.
 *
 * Proves the C++ matcher gives byte-identical verdicts to the Python matcher on
 * every vector in `normalizer_vectors.json` (read unchanged), and that the
 * test-only `mini_json` DOM rejects malformed, over-deep and duplicate-key
 * input without ever throwing or crashing. Also exercises `${var}` substitution
 * and `CaseRunner` skip resolution through a fake adapter.
 *
 * @par Test type
 * Unit / contract self-test for test infrastructure (label `conformance`).
 * @par On failure first check
 * A divergence from `tests/conformance/py/conformance_match.py`: the two
 * matchers share `normalizer_vectors.json` and must not drift. A single vector
 * failing here means the C++ rule port differs from the Python one.
 */

#define BOOST_TEST_MODULE ConformanceSupportSelftest
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "adapter.hpp"
#include "case_runner.hpp"
#include "corpus.hpp"
#include "matcher.hpp"
#include "mini_json.hpp"

using conformance::Case;
using conformance::CaseRunner;
using conformance::Expect;
using conformance::IConformanceAdapter;
using conformance::Json;
using conformance::Matcher;
using conformance::MatchResult;
using conformance::RunReport;
using conformance::Step;
using conformance::Transport;
using conformance::TransportCaps;
using conformance::VariableMap;

namespace
{

/// Reads the whole file at @p path, or returns an empty string on failure.
std::string read_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

/// Absolute path to the shared vector file (set via CONFORMANCE_CORPUS_DIR).
std::string vectors_path()
{
    return std::string(CONFORMANCE_CORPUS_DIR) + "/normalizer_vectors.json";
}

/// Evaluates one vector exactly like the Python harness `_run_vector`.
std::vector<std::string> run_vector(const Json& vector)
{
    VariableMap vars;
    if (const Json* variables = vector.find("variables")) {
        if (variables->is_object()) {
            for (const Json::Member& member : variables->members()) {
                vars[member.first] = member.second;
            }
        }
    }
    bool subset = false;
    if (const Json* mode = vector.find("mode"); mode && mode->is_string()) {
        subset = mode->as_string() == "subset";
    }

    std::string error;
    const Json* expected_in = vector.find("expected");
    const Json expected = Matcher::substitute(*expected_in, vars, &error);
    if (!error.empty()) {
        return {std::string("$: substitution error: ") + error};
    }

    Json actual = *vector.find("actual");
    std::vector<std::string> findings;
    if (const Json* normalize = vector.find("normalize");
        normalize && normalize->is_bool() && normalize->as_bool())
    {
        const Json* request = vector.find("request");
        bool carries_id = false;
        if (const Json* flag = vector.find("carries_id");
            flag && flag->is_bool())
        {
            carries_id = flag->as_bool();
        }
        actual =
            Matcher::normalize_actual(actual, request, carries_id, &findings);
    }

    const MatchResult result = Matcher::match(expected, actual, subset, vars);
    for (const std::string& reason : result.findings) {
        findings.push_back(reason);
    }
    return findings;
}

/// A canned adapter used by the skip-resolution and happy-path self-tests.
class FakeAdapter : public IConformanceAdapter
{
public:
    Transport transport() const override { return Transport::Direct; }

    TransportCaps caps() const override
    {
        TransportCaps caps;
        caps.carries_id = false;
        caps.carries_raw = false;
        caps.carries_options = true;
        return caps;
    }

    std::optional<std::string> begin_case(const Case&,
                                          const std::filesystem::path&) override
    {
        return std::nullopt;
    }

    std::string send(const Step&, const std::string&) override
    {
        return R"({"v":1,"id":"srv-1","ok":true,)"
               R"("result":{"slug":"docs","extra":9}})";
    }

    void end_case() override {}
};

/// Builds a minimal one-step case expecting the FakeAdapter's canned result.
Case make_ok_case()
{
    Case test_case;
    test_case.name = "selftest_ok";
    test_case.doc = "fake adapter happy path";
    test_case.command = "x.y";

    Step step;
    Json request = Json::object();
    request.set("v", Json::integer(1));
    request.set("command", Json::string("x.y"));
    request.set("args", Json::object());
    step.request = request;
    step.expect.present = true;
    step.expect.ok = true;
    step.expect.subset = true;
    step.expect.body = Expect::Body::Result;
    Json result = Json::object();
    result.set("slug", Json::string("docs"));
    step.expect.value = result;
    test_case.steps.push_back(std::move(step));
    return test_case;
}

}  // namespace

// ---------------------------------------------------------------------------
// Shared matcher/normalizer vectors (byte-identical to the Python harness)
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(every_vector_matches_its_expectation)
{
    const std::optional<Json> doc =
        Json::parse(read_file(vectors_path()), nullptr);
    BOOST_REQUIRE_MESSAGE(doc.has_value(), "vector file did not parse");
    BOOST_REQUIRE(doc->is_array());
    BOOST_TEST_MESSAGE("vector count: " << doc->items().size());
    BOOST_REQUIRE_GE(doc->items().size(), 60u);

    std::size_t checked = 0;
    for (const Json& vector : doc->items()) {
        const Json* name = vector.find("name");
        const std::string label =
            name != nullptr && name->is_string() ? name->as_string() : "?";
        const std::vector<std::string> reasons = run_vector(vector);
        const bool matched = reasons.empty();

        const Json* expect_match = vector.find("match");
        BOOST_REQUIRE(expect_match != nullptr && expect_match->is_bool());
        const bool want = expect_match->as_bool();
        std::string joined;
        for (const std::string& reason : reasons) {
            joined += "\n    " + reason;
        }
        BOOST_CHECK_MESSAGE(
            matched == want,
            label << ": expected match=" << want << " got reasons=" << joined);

        if (const Json* needle = vector.find("reason_contains");
            needle && needle->is_string())
        {
            bool found = false;
            for (const std::string& reason : reasons) {
                if (reason.find(needle->as_string()) != std::string::npos) {
                    found = true;
                }
            }
            BOOST_CHECK_MESSAGE(found,
                                label << ": no reason contains '"
                                      << needle->as_string() << "'" << joined);
        }
        ++checked;
    }
    BOOST_TEST(checked == doc->items().size());
}

// ---------------------------------------------------------------------------
// mini_json: round trips, malformed input, deep nesting, duplicate keys
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(json_round_trip_is_stable)
{
    const std::string text =
        R"({"a":[1,2,3],"b":{"c":"hi\n","d":true,"e":null},"f":1.5,"g":3.0})";
    const std::optional<Json> first = Json::parse(text, nullptr);
    BOOST_REQUIRE(first.has_value());
    const std::string dumped = first->dump();
    const std::optional<Json> second = Json::parse(dumped, nullptr);
    BOOST_REQUIRE(second.has_value());
    BOOST_TEST(dumped == second->dump());

    // Insertion order is preserved and an integral double keeps its kind.
    BOOST_REQUIRE(first->find("g") != nullptr);
    BOOST_TEST((first->find("g")->kind() == Json::Kind::Double));
    BOOST_REQUIRE(first->find("f") != nullptr);
    BOOST_TEST((first->find("f")->kind() == Json::Kind::Double));
    BOOST_REQUIRE(first->find("a") != nullptr);
    BOOST_REQUIRE(first->find("a")->at(2) != nullptr);
    BOOST_TEST((first->find("a")->at(2)->kind() == Json::Kind::Int));
}

BOOST_AUTO_TEST_CASE(malformed_json_is_rejected_without_crashing)
{
    const std::vector<std::string> malformed = {"",
                                                "{",
                                                "[",
                                                "{\"a\":}",
                                                "{\"a\":1,}",
                                                "[1,]",
                                                "nul",
                                                "tru",
                                                "fals",
                                                "{\"a\":1 \"b\":2}",
                                                "\"unterminated",
                                                "123abc",
                                                "{'a':1}",
                                                "[1 2]",
                                                "{\"a\"}",
                                                "--1",
                                                "01",
                                                "1.",
                                                "1e",
                                                "{\"a\":1,\"a\":2}"};
    BOOST_REQUIRE_GE(malformed.size(), 20u);
    for (const std::string& input : malformed) {
        std::string error;
        std::optional<Json> parsed;
        BOOST_CHECK_NO_THROW(parsed = Json::parse(input, &error));
        BOOST_CHECK_MESSAGE(!parsed.has_value(),
                            "expected rejection of: " << input);
        BOOST_CHECK_MESSAGE(!error.empty(),
                            "expected an error reason for: " << input);
    }
}

BOOST_AUTO_TEST_CASE(deeply_nested_input_fails_cleanly)
{
    std::string deep;
    const int levels = 200;
    for (int i = 0; i < levels; ++i) {
        deep.push_back('[');
    }
    for (int i = 0; i < levels; ++i) {
        deep.push_back(']');
    }
    std::string error;
    std::optional<Json> parsed;
    BOOST_CHECK_NO_THROW(parsed = Json::parse(deep, &error));
    BOOST_CHECK(!parsed.has_value());
    BOOST_CHECK(error == "max depth exceeded");
}

BOOST_AUTO_TEST_CASE(duplicate_keys_are_rejected)
{
    std::string error;
    const std::optional<Json> parsed = Json::parse(R"({"a":1,"a":2})", &error);
    BOOST_CHECK(!parsed.has_value());
    BOOST_CHECK(error == "duplicate key");
}

// ---------------------------------------------------------------------------
// Substitution
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(variable_substitution_expands_and_rewrites_tmp)
{
    VariableMap vars;
    vars["tmp"] = Json::string("C:\\proj\\run");
    vars["slug"] = Json::string("docs");
    Json input = Json::object();
    input.set("path", Json::string("${tmp}/pairs.csv"));
    input.set("name", Json::string("${slug}"));
    std::string error;
    const Json out = Matcher::substitute(input, vars, &error);
    BOOST_CHECK(error.empty());
    BOOST_REQUIRE(out.find("path") != nullptr);
    BOOST_TEST(out.find("path")->as_string() == "C:/proj/run/pairs.csv");
    BOOST_TEST(out.find("name")->as_string() == "docs");
}

BOOST_AUTO_TEST_CASE(unknown_variable_is_reported)
{
    VariableMap vars;
    Json input = Json::object();
    input.set("x", Json::string("${missing}"));
    std::string error;
    Matcher::substitute(input, vars, &error);
    BOOST_CHECK(error.find("unknown variable") != std::string::npos);
}

// ---------------------------------------------------------------------------
// CaseRunner skip resolution and happy path through a fake adapter
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(case_runner_resolves_skips_and_runs)
{
    CaseRunner runner([](int) {});
    FakeAdapter adapter;

    // 1) Happy path runs.
    {
        RunReport report = runner.run({make_ok_case()}, adapter);
        BOOST_TEST(report.ran == 1);
        BOOST_TEST(report.failed == 0);
        BOOST_TEST(report.skipped == 0);
    }

    // 2) transports.only excluding Direct is skipped.
    {
        Case test_case = make_ok_case();
        test_case.name = "only_tui";
        test_case.only.insert(Transport::Tui);
        RunReport report = runner.run({test_case}, adapter);
        BOOST_TEST(report.skipped == 1);
        BOOST_TEST(report.ran == 0);
    }

    // 3) transports.skip for Direct is skipped with the given reason.
    {
        Case test_case = make_ok_case();
        test_case.name = "skip_direct";
        test_case.skip[Transport::Direct] = "unsupported here";
        RunReport report = runner.run({test_case}, adapter);
        BOOST_TEST(report.skipped == 1);
        BOOST_REQUIRE(report.skips.size() == 1u);
        BOOST_TEST(report.skips[0].second == "unsupported here");
    }

    // 4) A raw_request step is skipped when the adapter cannot carry raw text.
    {
        Case test_case = make_ok_case();
        test_case.name = "needs_raw";
        test_case.steps[0].request.reset();
        test_case.steps[0].raw_request = std::string("not json");
        RunReport report = runner.run({test_case}, adapter);
        BOOST_TEST(report.skipped == 1);
    }
}

// ---------------------------------------------------------------------------
// Security regressions: path traversal and unbounded token arguments
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(case_runner_refuses_path_traversal_file_keys)
{
    namespace fs = std::filesystem;
    CaseRunner runner([](int) {});
    FakeAdapter adapter;

    const fs::path sentinel =
        fs::temp_directory_path() / "conformance_sentinel_07003.txt";
    {
        std::ofstream stream(sentinel, std::ios::binary);
        stream << "original";
    }

    // An absolute files key must be refused (contains a separator).
    {
        Case test_case = make_ok_case();
        test_case.name = "abs_key";
        test_case.files[sentinel.string()] = "HACKED";
        RunReport report = runner.run({test_case}, adapter);
        BOOST_TEST(report.failed == 1);
        BOOST_TEST(report.ran == 0);
    }

    // A parent-escaping `../` files key must be refused too.
    {
        Case test_case = make_ok_case();
        test_case.name = "dotdot_key";
        test_case.files["../escape.txt"] = "HACKED";
        RunReport report = runner.run({test_case}, adapter);
        BOOST_TEST(report.failed == 1);
        BOOST_TEST(report.ran == 0);
    }

    // The sentinel outside any temp dir is provably untouched.
    std::ifstream in(sentinel, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    BOOST_TEST(content == "original");
    std::error_code ec;
    fs::remove(sentinel, ec);
}

BOOST_AUTO_TEST_CASE(slug_token_with_overflowing_length_is_rejected_not_thrown)
{
    VariableMap vars;
    Json expected = Json::object();
    expected.set("slug", Json::string("<slug:99999999999999999999>"));
    Json actual = Json::object();
    actual.set("slug", Json::string("abc"));
    MatchResult result;
    BOOST_CHECK_NO_THROW(result =
                             Matcher::match(expected, actual, false, vars));
    BOOST_TEST(!result.ok);
}
