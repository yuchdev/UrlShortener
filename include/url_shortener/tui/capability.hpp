#pragma once

#include <functional>
#include <string>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
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
     * @brief Resolves a request's effective wire permission without executing
     * it, typically bound to @ref AsyncCaller::required_permission.
     *
     * Lets @ref can_run(const CommandInfo&, const std::string&) honor the
     * registry's argument-dependent permission raise (`permission_for`, D16) so
     * a read-only session's gate agrees with what the server would enforce for
     * the *specific* arguments.
     */
    using PermissionResolver =
        std::function<std::string(const commands::CommandRequest&)>;

    /**
     * @brief Builds the model for @p session with no argument-aware resolver.
     *
     * @ref can_run(const CommandInfo&) uses the descriptor's base permission;
     * the argument-aware @ref can_run(const CommandInfo&, const std::string&)
     * then also falls back to the base permission (correct only for commands
     * with no `permission_for` hook - construct with a resolver otherwise).
     *
     * @param session The session whose @ref SessionInfo::read_only flag and
     * identity drive every decision; copied, so the model does not
     * outlive-share any reference.
     */
    explicit CapabilityModel(SessionInfo session);

    /**
     * @brief Builds the model for @p session with an argument-aware resolver.
     *
     * @param session The session driving every decision; copied.
     * @param resolver Resolves a request's effective permission (D16); used by
     * @ref can_run(const CommandInfo&, const std::string&). Copied; a screen
     * typically binds it to @ref AsyncCaller::required_permission, whose owner
     * (@ref App) outlives the screen. An empty resolver degrades to base-
     * permission behaviour.
     */
    CapabilityModel(SessionInfo session, PermissionResolver resolver);

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
     * @brief Decides whether the session may run @p command with @p args_json,
     * honoring the argument-dependent permission raise (D16, T-116).
     *
     * Resolves the request's *effective* permission through the resolver passed
     * at construction (e.g. `link.list` with `status=all` resolves to `write`
     * even though its base permission is `read`) and applies the read-only rule
     * to that. With no resolver it falls back to the base permission of
     * @p command.
     *
     * @param command The command to classify; its @ref CommandInfo::name is
     * used to build the probe request.
     * @param args_json The exact `args` object text the screen would submit.
     * @return @ref Capability::allowed `true` with an empty reason when the
     * session may run the request; `false` with reason
     * `"not_authorized: read-only session"` when the session is read-only and
     * the resolved effective permission is not `"read"`.
     */
    Capability can_run(const CommandInfo& command,
                       const std::string& args_json) const;

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

    ///< Resolves a request's argument-dependent effective permission (D16);
    ///< empty when the model was built without one.
    PermissionResolver m_resolver_;
};

}  // namespace url_shortener::tui
