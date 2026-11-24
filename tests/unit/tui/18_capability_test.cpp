#define BOOST_TEST_MODULE TuiCapabilityTest

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>

/**
 * @file 18_capability_test.cpp
 * @brief Unit tests for @ref url_shortener::tui::CapabilityModel, the TUI's
 *        presentation mirror of the registry's authorization decision
 *        (subtask 06.0/18).
 *
 * Type: unit (pure model, url_shortener_tui_model, no FTXUI).
 * Scenario: the capability matrix (read / write / migrate / manage_users x
 *           read-only on/off) resolves as "read always, everything else only in
 *           a read/write session"; an unknown permission fails closed; an
 *           unknown command name reports `unknown_command`; the denial reason
 *           and the `READ-ONLY` badge text are the stable strings the goldens
 *           and the frame title depend on.
 * Boundaries: read vs non-read permission; read-only vs read/write session;
 *           known vs unknown command name; empty/garbage permission (fail
 *           closed).
 * On failure first check: CapabilityModel::can_run delegating to
 *           is_read_only_command, and the reason/badge string constants.
 */

namespace tui = url_shortener::tui;

namespace
{

tui::CommandInfo cmd(const std::string& name, const std::string& permission)
{
    tui::CommandInfo info;
    info.name = name;
    info.permission = permission;
    info.mutating = permission != "read";
    return info;
}

tui::SessionInfo session(bool read_only)
{
    tui::SessionInfo s;
    s.principal = "local-operator";
    s.transport = "tui";
    s.read_only = read_only;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// The full permission x read_only matrix: read is always allowed; write,
// migrate and manage_users are allowed only when the session is not read-only.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(permission_matrix)
{
    const std::vector<std::string> permissions = {
        "read", "write", "migrate", "manage_users"};

    // Read/write (Admin) session: everything is allowed with no reason.
    {
        const tui::CapabilityModel model(session(/*read_only=*/false));
        for (const std::string& perm : permissions) {
            BOOST_TEST_CONTEXT("permission=" << perm << " read_only=false")
            {
                const tui::Capability cap =
                    model.can_run(cmd("x." + perm, perm));
                BOOST_TEST(cap.allowed);
                BOOST_TEST(cap.reason.empty());
            }
        }
    }

    // Read-only (User) session: only read is allowed; the rest are denied with
    // the stable not_authorized reason.
    {
        const tui::CapabilityModel model(session(/*read_only=*/true));
        for (const std::string& perm : permissions) {
            BOOST_TEST_CONTEXT("permission=" << perm << " read_only=true")
            {
                const tui::Capability cap =
                    model.can_run(cmd("x." + perm, perm));
                if (perm == "read") {
                    BOOST_TEST(cap.allowed);
                    BOOST_TEST(cap.reason.empty());
                }
                else {
                    BOOST_TEST(!cap.allowed);
                    BOOST_TEST(cap.reason
                               == "not_authorized: read-only session");
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Fail closed: an unrecognized / empty permission is treated as a write and is
// denied in a read-only session (never silently enabled).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unknown_permission_fails_closed)
{
    const tui::CapabilityModel model(session(/*read_only=*/true));

    const tui::Capability garbage = model.can_run(cmd("x.weird", "sudo"));
    BOOST_TEST(!garbage.allowed);
    BOOST_TEST(garbage.reason == "not_authorized: read-only session");

    const tui::Capability empty = model.can_run(cmd("x.empty", ""));
    BOOST_TEST(!empty.allowed);
    BOOST_TEST(empty.reason == "not_authorized: read-only session");
}

// ---------------------------------------------------------------------------
// The name overload resolves against the catalog and reports unknown_command
// for a name that is not present.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(resolve_by_name_against_catalog)
{
    const std::vector<tui::CommandInfo> catalog = {
        cmd("link.list", "read"),
        cmd("link.create", "write"),
    };

    const tui::CapabilityModel read_only(session(/*read_only=*/true));

    const tui::Capability list = read_only.can_run("link.list", catalog);
    BOOST_TEST(list.allowed);

    const tui::Capability create = read_only.can_run("link.create", catalog);
    BOOST_TEST(!create.allowed);
    BOOST_TEST(create.reason == "not_authorized: read-only session");

    const tui::Capability missing = read_only.can_run("no.such", catalog);
    BOOST_TEST(!missing.allowed);
    BOOST_TEST(missing.reason == "unknown_command");

    // In a read/write session a known write command resolves to allowed.
    const tui::CapabilityModel admin(session(/*read_only=*/false));
    BOOST_TEST(admin.can_run("link.create", catalog).allowed);
    // An unknown name is unknown_command regardless of session mode.
    BOOST_TEST(!admin.can_run("no.such", catalog).allowed);
    BOOST_TEST(admin.can_run("no.such", catalog).reason == "unknown_command");
}

// ---------------------------------------------------------------------------
// read_only() mirrors the session, and badge() is the stable text the frame
// title and the d01 golden depend on.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(read_only_flag_and_badge_text)
{
    const tui::CapabilityModel read_only(session(/*read_only=*/true));
    BOOST_TEST(read_only.read_only());
    BOOST_TEST(read_only.badge() == "READ-ONLY");

    const tui::CapabilityModel admin(session(/*read_only=*/false));
    BOOST_TEST(!admin.read_only());
    BOOST_TEST(admin.badge().empty());
}
