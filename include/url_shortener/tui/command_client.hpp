#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::tui
{

/**
 * @brief The TUI-local classification of a command argument's shape.
 *
 * This is the view-model mirror of `commands::ArgType` (ADR 0002 section 2):
 * it carries exactly the schema vocabulary a form or palette needs to render a
 * field, with no dependency on the registry or its validation. The mapping
 * from `commands::ArgType` is total; an unrecognized registry type maps to
 * @ref ArgKind::string so a new type can never crash the catalog. Enumerator
 * order is not contractual.
 */
enum class ArgKind
{
    ///< A free-text string field (`commands::ArgType::string`).
    string,

    ///< An integer field with optional min/max bounds
    ///< (`commands::ArgType::integer`).
    integer,

    ///< A boolean checkbox (`commands::ArgType::boolean`).
    boolean,

    ///< A closed set of string choices, listed in @ref ArgInfo::enum_values
    ///< (`commands::ArgType::enumeration`).
    enumeration,

    ///< A list of strings (`commands::ArgType::string_list`).
    string_list,

    ///< A flat string-to-string map (`commands::ArgType::string_map`).
    string_map,

    ///< A nested object whose members are named in @ref ArgInfo::members
    ///< (`commands::ArgType::object`).
    object,

    ///< An RFC 3339 timestamp string (`commands::ArgType::timestamp`).
    timestamp
};

/**
 * @brief One command argument as the TUI needs to render and describe it.
 *
 * Built from a `commands::ArgSpec` by @ref InProcessClient::commands. It is a
 * pure value type: copying it is cheap and it owns all of its strings, so a
 * screen may keep it after the client that produced it is gone.
 */
struct ArgInfo
{
    ///< Argument key as it appears in the request `args` object.
    std::string name;

    ///< The argument's rendered shape; drives which form widget is shown.
    ArgKind kind = ArgKind::string;

    ///< `true` when the argument must be supplied for the command to run.
    bool required = false;

    ///< `true` when an explicit JSON `null` is an accepted value, so the form
    ///< may offer a "clear" affordance (e.g. `link.update` `expires_at` /
    ///< `campaign`). Copied from `commands::ArgSpec::nullable`.
    bool nullable = false;

    ///< Allowed values when @ref kind is @ref ArgKind::enumeration; empty for
    ///< every other kind.
    std::vector<std::string> enum_values;

    ///< Member names when @ref kind is @ref ArgKind::object, copied from the
    ///< nested `commands::ArgSpec::fields` (e.g. a campaign's
    ///< `name,source,medium,term,content,id`); empty for every other kind.
    std::vector<std::string> members;

    ///< The documented default applied by the handler when the argument is
    ///< omitted, as a raw JSON token; `std::nullopt` when the argument has no
    ///< documented default.
    std::optional<std::string> default_value;

    ///< One-line human description for help/forms, copied verbatim from the
    ///< descriptor; a raw server string (never sanitized here, see T-110).
    std::string doc;
};

/**
 * @brief One command as the TUI needs to list, authorize and render it.
 *
 * Built from a `commands::CommandDescriptor` by @ref InProcessClient::commands,
 * or filled by a future `RemoteClient` from a `command.list` result. A pure,
 * self-owning value type with no registry types in it (contract C8).
 */
struct CommandInfo
{
    ///< Dotted command name, e.g. `"link.create"`.
    std::string name;

    ///< One-line human summary for the catalog and palette.
    std::string summary;

    ///< The base permission the command requires, as a wire string:
    ///< `"read"`, `"write"`, `"migrate"` or `"manage_users"`. An
    ///< argument-dependent permission hook may raise this at execution time;
    ///< only the descriptor's base value is reported here.
    std::string permission;

    ///< `true` when the command runs asynchronously as a job (its result is a
    ///< job handle, not an immediate payload).
    bool is_job = false;

    ///< `true` when the command changes state; the TUI shows a confirm dialog
    ///< for such commands (ADR 0003 Decision 2 rule 4).
    bool mutating = false;

    ///< `true` when the command honors `options.dry_run` (a validate-only
    ///< run); always implies @ref mutating.
    bool supports_dry_run = false;

    ///< The command's argument schema in descriptor order.
    std::vector<ArgInfo> args;
};

/**
 * @brief The one predicate the TUI uses to decide whether a command is a
 * read-only (non-mutating-capability) command.
 *
 * Shared by every capability gate - the palette's read-only disabling, the
 * form's disabled submit button, and @ref MutationFlow's defence-in-depth
 * refusal - so they can never disagree (threat model T-116). It is a pure
 * function of the descriptor's base permission and **fails closed**: only the
 * exact wire string `"read"` counts as read-only; any other value, including an
 * empty permission, is treated as a potential write so a read-only session
 * refuses it. Authorization itself still rests with the registry (contract C7);
 * this only decides client-side friction/disabling.
 *
 * @param info The command to classify.
 * @return `true` iff @ref CommandInfo::permission is exactly `"read"`.
 */
inline bool is_read_only_command(const CommandInfo& info)
{
    return info.permission == "read";
}

/**
 * @brief The identity and mode of the current client session.
 *
 * A snapshot describing who the screens render for and whether writes are
 * allowed. Produced by @ref ICommandClient::session; the TUI uses
 * @ref read_only to render write controls disabled rather than hidden
 * (ADR 0003 Decision 2 rule 5). Authorization itself is never decided from
 * this struct - the registry remains the sole authority (contract C7).
 */
struct SessionInfo
{
    ///< Human/service identity for the status bar, e.g. `"local-operator"`.
    ///< Diagnostic only; not an authorization input.
    std::string principal;

    ///< `true` when the session cannot perform write/migrate commands (the
    ///< caller runs as `ControlSet::User`). Presentation hint only; the
    ///< registry still enforces every permission.
    bool read_only = false;

    ///< Originating transport label; `"tui"` for an in-process TUI session.
    std::string transport = "tui";
};

/**
 * @brief The single seam through which the TUI reaches the application
 * (ADR 0003 Decision 2 rule 1, contract C8).
 *
 * Every screen builds a `commands::CommandRequest` and renders the returned
 * `commands::CommandResponse`; nothing below this interface (a registry,
 * service, repository or DTO) is visible to a screen. The default
 * implementation is @ref InProcessClient over `commands::CommandRegistry`; a
 * future `RemoteClient` can satisfy the same interface over REST/IPC without
 * changing any screen. All three methods are pure virtual; an implementation
 * owns its own thread-safety contract (see @ref InProcessClient).
 */
class ICommandClient
{
public:
    /**
     * @brief Virtual destructor; clients are held through this base.
     */
    virtual ~ICommandClient() = default;

    /**
     * @brief Executes one command and returns its response envelope.
     *
     * Blocking: it runs the command to completion (or, for a job-backed
     * command, to acceptance) before returning. It MUST be called off the UI
     * thread and the response posted back to the screen (ADR 0003 Decision 2
     * rule 3, subtask 03); calling it on the UI thread freezes the terminal.
     *
     * @param request The command to run; its `id` is echoed in the response.
     * @return The response envelope. Implementations guarantee a well-formed,
     * self-consistent envelope: a handler failure arrives as an `error`
     * payload, never as a thrown exception, and an ambiguous or self-
     * contradictory envelope is replaced by an `internal` error (threat model
     * T-113). Result strings are the server's own bytes, not sanitized for a
     * terminal here (that happens at the render sinks, T-110).
     */
    virtual commands::CommandResponse call(
        const commands::CommandRequest& request) = 0;

    /**
     * @brief Returns a snapshot of the command catalog.
     *
     * The order and contents are the implementation's: @ref InProcessClient
     * lists every registered descriptor (ascending by name); a `RemoteClient`
     * fills it from a `command.list` result.
     *
     * @return One @ref CommandInfo per reachable command; a self-owning copy
     * the caller may retain.
     */
    virtual std::vector<CommandInfo> commands() const = 0;

    /**
     * @brief Returns the current session's identity and mode.
     *
     * @return A @ref SessionInfo describing the principal, read-only state and
     * transport of this client.
     */
    virtual SessionInfo session() const = 0;
};

}  // namespace url_shortener::tui
