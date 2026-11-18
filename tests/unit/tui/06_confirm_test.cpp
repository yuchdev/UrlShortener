#define BOOST_TEST_MODULE TuiConfirmTest

#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/text_fit.hpp>

#include "hostile_strings.hpp"

/**
 * @file 06_confirm_test.cpp
 * @brief Unit tests for the D10 confirm policy and model (FTXUI-free).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: the policy maps non-mutating to none, a generic mutation to yes_no,
 *           link.delete to a typed-slug confirmation, a migrate command to a
 *           typed "MIGRATE" confirmation, and --yes to none for everything;
 *           the model accepts a submit only on an exact raw-token match; and a
 *           slug carrying a bidi override is displayed sanitized while the
 *           comparison runs on the raw bytes (T-110).
 * Boundaries: exact vs case/space/empty mismatch; missing-slug fallback.
 * On failure first check: the decision order in `ConfirmPolicy::decide` or the
 *           raw-byte comparison in `ConfirmModel::can_submit`.
 */

using url_shortener::tui::CommandInfo;
using url_shortener::tui::ConfirmKind;
using url_shortener::tui::ConfirmModel;
using url_shortener::tui::ConfirmPolicy;
using url_shortener::tui::ConfirmRequest;
using url_shortener::tui::sanitize_for_terminal;
using url_shortener::tui::testing::bidi_override_slug;
using url_shortener::tui::testing::has_display_hazard;

namespace
{

CommandInfo make_info(const std::string& name,
                      bool mutating,
                      const std::string& permission)
{
    CommandInfo info;
    info.name = name;
    info.mutating = mutating;
    info.permission = permission;
    return info;
}

}  // namespace

// ---------------------------------------------------------------------------
// Policy matrix: non-mutating -> none; generic mutation -> yes_no;
// link.delete -> type_token(slug); migrate -> type_token("MIGRATE").
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(policy_matrix)
{
    const ConfirmPolicy policy(false);

    BOOST_TEST((policy.decide(make_info("link.get", false, "read"), "{}").kind
                == ConfirmKind::none));

    BOOST_TEST(
        (policy.decide(make_info("link.create", true, "write"), "{}").kind
         == ConfirmKind::yes_no));

    const ConfirmRequest del = policy.decide(
        make_info("link.delete", true, "write"), "{\"slug\":\"promo\"}");
    BOOST_TEST((del.kind == ConfirmKind::type_token));
    BOOST_TEST(del.token == "promo");
    BOOST_TEST(del.prompt.find("promo") != std::string::npos);

    const ConfirmRequest mig =
        policy.decide(make_info("db.migrate", true, "migrate"), "{}");
    BOOST_TEST((mig.kind == ConfirmKind::type_token));
    BOOST_TEST(mig.token == "MIGRATE");
}

// ---------------------------------------------------------------------------
// link.delete with no slug falls back to a plain yes_no confirm.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(delete_without_slug_falls_back_to_yes_no)
{
    const ConfirmPolicy policy(false);
    const ConfirmRequest req =
        policy.decide(make_info("link.delete", true, "write"), "{}");
    BOOST_TEST((req.kind == ConfirmKind::yes_no));
}

// ---------------------------------------------------------------------------
// --yes collapses every decision to none, including the typed-slug delete
// (threat model T-117).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(assume_yes_skips_all_confirms)
{
    const ConfirmPolicy policy(true);

    BOOST_TEST(
        (policy.decide(make_info("link.create", true, "write"), "{}").kind
         == ConfirmKind::none));
    BOOST_TEST((policy
                    .decide(make_info("link.delete", true, "write"),
                            "{\"slug\":\"promo\"}")
                    .kind
                == ConfirmKind::none));
    BOOST_TEST((policy.decide(make_info("db.migrate", true, "migrate"), "{}")
                    .kind
                == ConfirmKind::none));
}

// ---------------------------------------------------------------------------
// type_token submission requires an exact, case-sensitive raw-byte match.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(type_token_exact_match_only)
{
    ConfirmRequest req;
    req.kind = ConfirmKind::type_token;
    req.token = "promo";
    ConfirmModel model(req);

    BOOST_TEST(!model.can_submit());  // empty
    model.set_input("Promo");
    BOOST_TEST(!model.can_submit());  // case
    model.set_input("promo ");
    BOOST_TEST(!model.can_submit());  // trailing space
    model.set_input("promo");
    BOOST_TEST(model.can_submit());

    // Enter before a match is a no-op; after a match it submits.
    ConfirmModel pre(req);
    pre.submit();
    BOOST_TEST(!pre.result().has_value());
    pre.set_input("promo");
    pre.submit();
    BOOST_REQUIRE(pre.result().has_value());
    BOOST_TEST(pre.result().value());
}

// ---------------------------------------------------------------------------
// yes_no confirms immediately; cancel records a false result.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(yes_no_submit_and_cancel)
{
    ConfirmRequest req;
    req.kind = ConfirmKind::yes_no;
    ConfirmModel model(req);
    BOOST_TEST(model.can_submit());

    ConfirmModel cancelled(req);
    cancelled.cancel();
    BOOST_REQUIRE(cancelled.result().has_value());
    BOOST_TEST(!cancelled.result().value());
}

// ---------------------------------------------------------------------------
// A slug with quotes is kept raw in the token (prompt still shows it).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(slug_with_quotes_kept_raw)
{
    const ConfirmPolicy policy(false);
    const ConfirmRequest req = policy.decide(
        make_info("link.delete", true, "write"),
        "{\"slug\":\"a\\\"b\"}");  // slug = a"b
    BOOST_TEST((req.kind == ConfirmKind::type_token));
    BOOST_TEST(req.token == "a\"b");
}

// ---------------------------------------------------------------------------
// T-110: a slug carrying a bidi override (U+202E) is DISPLAYED sanitized while
// can_submit() matches only the RAW bytes - typing the sanitized form fails.
// On failure first check: whether decide() sanitized the prompt but kept a raw
// token, and whether can_submit compares the raw token.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(bidi_slug_displayed_sanitized_matched_raw)
{
    const std::string raw_slug = bidi_override_slug();  // contains U+202E
    const std::string args = "{\"slug\":\"" + raw_slug + "\"}";

    const ConfirmPolicy policy(false);
    const ConfirmRequest req =
        policy.decide(make_info("link.delete", true, "write"), args);

    BOOST_TEST((req.kind == ConfirmKind::type_token));
    // The displayed prompt is terminal-safe (the override is stripped).
    BOOST_TEST(!has_display_hazard(req.prompt));
    // The token still holds the raw bytes, including the override.
    BOOST_TEST(req.token == raw_slug);

    ConfirmModel model(req);
    // Typing the sanitized (displayed) form does NOT satisfy the guard.
    model.set_input(sanitize_for_terminal(raw_slug));
    BOOST_TEST(!model.can_submit());
    // Only the exact raw bytes do.
    model.set_input(raw_slug);
    BOOST_TEST(model.can_submit());
}
