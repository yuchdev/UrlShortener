#pragma once

#include <string>
#include <vector>

#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @file capability.hpp
 * @brief The one place the TUI mirrors the registry's authorization decision
 * for presentation (ADR 0003 Decision 2 rule 5, subtask 06.0/18).
 *
 * @ref CapabilityModel answers "may this session run this command?" so screens
 * render a write control **disabled with the registry's `not_authorized`
 * reason, not hidden**, and the status badge reads `READ-ONLY`. It is a pure
 * value model (FTXUI-free, `url_shortener_tui_model`) built once from a
 * @ref SessionInfo and consulted by the frame, the palette, the forms and the
 * per-screen action gates so they can never disagree.
 *
 * Contract C7 (authorization lives in the registry): this model is presentation
 * only and is never the sole guard. The registry's `AccessGuard` still enforces
 * every permission at execution time; if a stale UI path reaches a denied
 * action anyway, @ref MutationFlow refuses it client-side (threat model T-116)
 * and, failing even that, the server answers `not_authorized`. The reason text
 * deliberately reuses the registry's `not_authorized` code so what the UI shows
 * before a call and what the server would answer agree.
 */

/**
 * @brief The capability decision for one command in the current session.
 *
 * A pure value type: @ref allowed says whether the session may run the command,
 * and @ref reason carries a non-empty, value-free explanation when it may not
 * (never a secret, id or argument value - constraint C7 / "No secrets in
 * reasons").
 */
struct Capability
{
    ///< `true` when the session may run the command (a read command always, a
    ///< write/migrate/manage_users command only outside a read-only session).
    bool allowed = false;

    ///< Empty when @ref allowed; otherwise the display reason. A read-only
    ///< denial reads `"not_authorized: read-only session"`; an unknown command
    ///< reads `"unknown_command"`. Value-free (no ids/secrets).
    std::string reason;
};

/**
 * @brief Mirrors the registry's permission decision for the current session so
 * the TUI can disable (never hide) actions the session cannot run.
 *
 * Built once per session from a @ref SessionInfo. The capability decision rests
 * entirely on @ref SessionInfo::read_only and the command's base permission
 * (via @ref is_read_only_command, which **fails closed** - only the exact wire
 * string `"read"` counts as read-only). A read/write (Admin) session may run
 * every command; a read-only (`ControlSet::User`) session may run only read
 * commands. The model is immutable after construction and cheap to copy.
 */
class CapabilityModel
{
public:
    /**
     * @brief Builds the model for @p session.
     *
     * @param session The session whose @ref SessionInfo::read_only flag and
     * identity drive every decision; copied, so the model does not
     * outlive-share any reference.
     */
    explicit CapabilityModel(SessionInfo session);

    /**
     * @brief Decides whether the session may run @p command.
     *
     * @param command The command to classify; only its
     * @ref CommandInfo::permission is read.
     * @return @ref Capability::allowed `true` with an empty reason for a read
     * command, or in any non-read-only session; `false` with reason
     * `"not_authorized: read-only session"` when the session is read-only and
     * @p command is not a read command (write / migrate / manage_users, or any
     * unrecognized permission, since the classifier fails closed).
     */
    Capability can_run(const CommandInfo& command) const;

    /**
     * @brief Decides whether the session may run the command named
     * @p command_name, resolved against @p catalog.
     *
     * @param command_name The dotted command name to look up.
     * @param catalog The command catalog to resolve @p command_name in
     * (typically @ref ICommandClient::commands).
     * @return @ref Capability::allowed `false` with reason `"unknown_command"`
     * when no catalog entry matches @p command_name; otherwise the same
     * decision as @ref can_run(const CommandInfo&) for the matched command.
     */
    Capability can_run(const std::string& command_name,
                       const std::vector<CommandInfo>& catalog) const;

    /**
     * @brief Whether this session runs read-only (`ControlSet::User`).
     *
     * @return The session's @ref SessionInfo::read_only flag.
     */
    bool read_only() const;

    /**
     * @brief The status badge the frame shows for this session.
     *
     * @return `"READ-ONLY"` for a read-only session; an empty string otherwise.
     */
    std::string badge() const;

private:
    ///< The session this model mirrors; copied at construction.
    SessionInfo m_session_;
};

}  // namespace url_shortener::tui
