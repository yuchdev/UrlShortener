/**
 * @file capability.cpp
 * @brief Implements @ref url_shortener::tui::CapabilityModel, the TUI's
 * presentation mirror of the registry's authorization decision (subtask
 * 06.0/18). Every decision defers to @ref is_read_only_command so the frame,
 * palette, forms and per-screen gates share one fail-closed rule and the shown
 * reason agrees with the server's `not_authorized` code (contract C7).
 */
#include <utility>

#include "url_shortener/tui/capability.hpp"

namespace url_shortener::tui
{

namespace
{

/// The display reason for a read-only denial; reuses the registry's
/// `not_authorized` code so the pre-call hint and the server's answer agree.
constexpr char k_read_only_reason[] = "not_authorized: read-only session";

/// The display reason when a name does not resolve to any catalog command.
constexpr char k_unknown_reason[] = "unknown_command";

}  // namespace

CapabilityModel::CapabilityModel(SessionInfo session)
    : m_session_(std::move(session))
{
}

CapabilityModel::CapabilityModel(SessionInfo session,
                                 PermissionResolver resolver)
    : m_session_(std::move(session))
    , m_resolver_(std::move(resolver))
{
}

Capability CapabilityModel::can_run(const CommandInfo& command) const
{
    // A read/write session runs everything; the registry still enforces the
    // real permission (C7). A read-only session runs only read commands, with
    // the classifier failing closed on any non-`read` or unknown permission.
    if (!m_session_.read_only || is_read_only_command(command)) {
        return Capability {true, std::string()};
    }
    return Capability {false, std::string(k_read_only_reason)};
}

Capability CapabilityModel::can_run(const CommandInfo& command,
                                    const std::string& args_json) const
{
    // Without a resolver, no argument-dependent raise is known: fall back to
    // the base-permission decision (correct for commands with no
    // permission_for).
    if (!m_resolver_) {
        return can_run(command);
    }
    if (!m_session_.read_only) {
        return Capability {true, std::string()};
    }

    commands::CommandRequest probe;
    probe.command = command.name;
    probe.args_json = args_json;
    // The effective permission for these exact arguments; the resolver fails
    // closed (non-`read`) on anything it cannot resolve.
    const std::string effective = m_resolver_(probe);
    if (effective == "read") {
        return Capability {true, std::string()};
    }
    return Capability {false, std::string(k_read_only_reason)};
}

Capability CapabilityModel::can_run(
    const std::string& command_name,
    const std::vector<CommandInfo>& catalog) const
{
    for (const CommandInfo& command : catalog) {
        if (command.name == command_name) {
            return can_run(command);
        }
    }
    return Capability {false, std::string(k_unknown_reason)};
}

bool CapabilityModel::read_only() const
{
    return m_session_.read_only;
}

std::string CapabilityModel::badge() const
{
    return m_session_.read_only ? std::string("READ-ONLY") : std::string();
}

}  // namespace url_shortener::tui
