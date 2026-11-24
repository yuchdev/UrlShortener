/** @file 24_command_api_caller.cpp
 *  @brief Characterizes the command-API configuration gate helpers added for
 *  the gated HTTP surface (milestone 0006, Task 04.0 subtask 08): the
 *  secure-by-default flags, the loopback-peer classifier, and the fail-closed
 *  caller every gated route runs as.
 *
 *  These helpers are the entire access-control perimeter for the new HTTP
 *  surface until Milestone 0002 sessions exist (threat model T-64), so the
 *  matrix below is exhaustive: the default is off/user, `admin` is honoured only
 *  for a loopback peer, and an absent peer fails closed to `User`.
 */
#define BOOST_TEST_MODULE CommandApiCaller
#include <boost/test/unit_test.hpp>

#include <optional>
#include <string>

#include <boost/asio/ip/address.hpp>

#include <url_shortener/core/config.hpp>
#include <url_shortener/http/command_bridge.hpp>

using url_shortener::http::command_api_caller;
using url_shortener::http::command_api_control_set;
using url_shortener::http::command_api_enabled;
using url_shortener::http::is_loopback_peer;

namespace
{

boost::asio::ip::address addr(const std::string& text)
{
    return boost::asio::ip::make_address(text);
}

ServerConfig config_with_role(const std::string& role, const bool enabled)
{
    ServerConfig config;
    config.command_api_role = role;
    config.command_api_enabled = enabled;
    return config;
}

}  // namespace

// Type: unit (secure defaults).
// Scenario: a default-constructed ServerConfig has the command API disabled and
//   the least-privilege role, and command_api_enabled mirrors the flag.
// Boundaries: the default is security-load-bearing (threat model T-64); it must
//   stay false/user.
// On failure first check: did a default flip, or did command_api_enabled stop
//   reading the flag?
BOOST_AUTO_TEST_CASE(defaults_are_off_and_user)
{
    const ServerConfig defaults;
    BOOST_TEST(defaults.command_api_enabled == false);
    BOOST_TEST(defaults.command_api_role == "user");

    BOOST_TEST(command_api_enabled(defaults) == false);
    BOOST_TEST(command_api_enabled(config_with_role("user", true)) == true);
}

// Type: unit (role mapping before the peer check).
// Scenario: command_api_control_set maps exactly "admin" to Admin and every
//   other value (user, empty, mixed case) to User.
// Boundaries: the token is case-sensitive; only "admin" escalates.
// On failure first check: did the role comparison become case-insensitive or
//   accept another token?
BOOST_AUTO_TEST_CASE(control_set_maps_only_exact_admin)
{
    BOOST_TEST((command_api_control_set(config_with_role("admin", true))
                == ControlSet::Admin));
    BOOST_TEST((command_api_control_set(config_with_role("user", true))
                == ControlSet::User));
    BOOST_TEST((command_api_control_set(config_with_role("Admin", true))
                == ControlSet::User));
    BOOST_TEST((command_api_control_set(config_with_role("", true))
                == ControlSet::User));
}

// Type: unit (loopback classifier).
// Scenario: 127.0.0.0/8, ::1 and IPv4-mapped loopback classify as loopback;
//   routable v4/v6 and IPv4-mapped routable do not.
// Boundaries: the whole 127/8 block is loopback, not just 127.0.0.1; a mapped
//   10.x is not.
// On failure first check: did the 127/8 mask drop, or did v4-mapped decoding
//   mis-handle the embedded octet?
BOOST_AUTO_TEST_CASE(loopback_peer_matrix)
{
    BOOST_TEST(is_loopback_peer(addr("127.0.0.1")));
    BOOST_TEST(is_loopback_peer(addr("127.0.0.2")));
    BOOST_TEST(is_loopback_peer(addr("127.255.255.254")));
    BOOST_TEST(is_loopback_peer(addr("::1")));
    BOOST_TEST(is_loopback_peer(addr("::ffff:127.0.0.2")));

    BOOST_TEST(!is_loopback_peer(addr("10.0.0.5")));
    BOOST_TEST(!is_loopback_peer(addr("192.168.1.2")));
    BOOST_TEST(!is_loopback_peer(addr("8.8.8.8")));
    BOOST_TEST(!is_loopback_peer(addr("::ffff:10.0.0.1")));
    BOOST_TEST(!is_loopback_peer(addr("2001:db8::1")));
}

// Type: unit (fail-closed caller matrix).
// Scenario: the effective control set is Admin only for role admin AND a
//   loopback peer; role user is always User, and role admin from a routable or
//   absent peer is User.
// Boundaries: an absent peer (std::nullopt) is non-loopback, so it never
//   escalates (threat model T-64/T-70).
// On failure first check: did the AND become an OR, or did a nullopt peer start
//   defaulting to Admin?
BOOST_AUTO_TEST_CASE(caller_is_admin_only_for_admin_and_loopback)
{
    const ServerConfig admin = config_with_role("admin", true);
    const ServerConfig user = config_with_role("user", true);

    BOOST_TEST((command_api_caller(admin, addr("127.0.0.1")).control_set
                == ControlSet::Admin));
    BOOST_TEST((command_api_caller(admin, addr("::1")).control_set
                == ControlSet::Admin));
    BOOST_TEST((command_api_caller(admin, addr("::ffff:127.0.0.2")).control_set
                == ControlSet::Admin));

    BOOST_TEST((command_api_caller(admin, addr("10.0.0.5")).control_set
                == ControlSet::User));
    BOOST_TEST((command_api_caller(admin, addr("192.168.1.2")).control_set
                == ControlSet::User));
    BOOST_TEST((command_api_caller(admin, addr("::ffff:10.0.0.1")).control_set
                == ControlSet::User));
    BOOST_TEST((command_api_caller(admin, std::nullopt).control_set
                == ControlSet::User));

    BOOST_TEST((command_api_caller(user, addr("127.0.0.1")).control_set
                == ControlSet::User));
    BOOST_TEST((command_api_caller(user, std::nullopt).control_set
                == ControlSet::User));
}

// Type: unit (fixed non-identifying audit labels, threat model T-84).
// Scenario: the caller's principal/transport are the fixed command-api/rest
//   labels and never carry the peer address.
// Boundaries: the labels are constants; they must not be derived from the peer.
// On failure first check: did an adapter start deriving a label from the peer?
BOOST_AUTO_TEST_CASE(caller_labels_are_fixed_and_non_identifying)
{
    const auto caller =
        command_api_caller(config_with_role("admin", true), addr("127.0.0.1"));
    BOOST_TEST(caller.principal == "command-api");
    BOOST_TEST(caller.transport == "rest");
    BOOST_TEST(caller.principal.find("127.0.0.1") == std::string::npos);
}
