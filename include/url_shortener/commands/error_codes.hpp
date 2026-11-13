#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/app/app_error.hpp>

namespace url_shortener::commands
{

/**
 * @brief The closed vocabulary of error codes carried by a command response
 * envelope (`error.code`).
 *
 * This is the single source of truth for command-layer failure
 * classification. Every transport (REST generic endpoint, CLI `cmd`, stdio,
 * TUI, future IPC) reports failures using these names, and the one table in
 * @ref all_error_infos maps each enumerator to its wire name, HTTP status and
 * CLI exit code. No other file in `src/commands/` may switch on this enum to
 * derive a status or exit code (milestone contract C1).
 *
 * Enumerator order is contractual: @ref error_info_for indexes the table by
 * `static_cast<std::size_t>(code)`, so the table in @ref all_error_infos is
 * built in exactly this order.
 */
enum class CommandErrorCode
{
    ///< No error; the operation completed as requested. Not emitted in a
    ///< failure envelope.
    none,

    ///< The request envelope itself was rejected: not valid JSON, missing the
    ///< `command` field, an unknown top-level/`options` key, or a bad `v`.
    invalid_envelope,

    ///< The `command` name did not resolve to any registered command.
    unknown_command,

    ///< An argument was missing, unknown, ill-typed, or otherwise failed its
    ///< `ArgSpec` validation.
    invalid_field,

    ///< The target URL failed validation. Deliberately indistinguishable
    ///< between a malformed URL and a target blocked as a private/SSRF host:
    ///< both map here with the same status and a value-free message so the
    ///< create path cannot be used as an internal-network probe.
    invalid_url,

    ///< The supplied slug failed format validation.
    invalid_slug,

    ///< The supplied slug is on the system reserved list and cannot be used
    ///< for a user-created link.
    reserved_slug,

    ///< The slug is already in use by an existing link; nothing was persisted.
    slug_conflict,

    ///< The addressed resource (link, job, or table) was not found.
    not_found,

    ///< The caller's role lacks the permission the command requires.
    not_authorized,

    ///< The requested mode (`dry_run`/`async`) is not supported for this
    ///< command, or the command is a not-yet-enabled feature placeholder.
    unsupported,

    ///< The job queue is full and the request could not be accepted.
    busy,

    ///< A job was terminated by `job.cancel`; surfaced through `job.get`.
    job_cancelled,

    ///< The underlying storage backend failed; no mutation was persisted.
    storage_failure,

    ///< An unexpected internal failure not attributable to caller input. The
    ///< accompanying message is always fixed text and never exception detail.
    internal
};

/**
 * @brief A command-layer error: a stable @ref CommandErrorCode plus a
 * human-readable message.
 *
 * The `code` is the stable contract clients branch on; the `message` is for
 * humans and may change between versions. The message must never contain an
 * argument *value* (only fixed text or an argument *name*), so the struct is
 * safe to log and to return to an untrusted caller (milestone contract C6).
 */
struct CommandError
{
    ///< Failure category; `none` on a default-constructed instance.
    CommandErrorCode code = CommandErrorCode::none;

    ///< Human-readable detail. Never contains argument values; callers pass
    ///< fixed strings or an argument name only.
    std::string message;

    /**
     * @brief Indicates whether this represents success.
     * @return `true` when `code == CommandErrorCode::none`; `false` otherwise.
     */
    bool ok() const { return code == CommandErrorCode::none; }
};

/**
 * @brief One row of the error table: everything known about a single
 * @ref CommandErrorCode.
 *
 * All six columns live in the same struct so there is exactly one place that
 * maps a code to a status or exit code. The `legacy_*` columns describe how
 * the existing REST **resource routes** render the equivalent
 * `app::AppErrorCode` today; they are kept unchanged to honor milestone
 * contract C3 (existing REST bodies and statuses do not change) and may be
 * dropped when the resource routes are retired.
 */
struct error_info
{
    ///< The code this row describes.
    CommandErrorCode code;

    ///< Wire value of `error.code` in the response envelope. Unique across the
    ///< table and lower_snake_case.
    std::string_view name;

    ///< HTTP status returned by the generic `POST /api/v1/commands` endpoint
    ///< for this code.
    unsigned http_status;

    ///< Process exit code returned by the CLI `cmd` adapter for this code.
    int exit_code;

    ///< HTTP status the existing REST resource routes return today for the
    ///< equivalent `app::AppErrorCode` (from `statusForAppError`). Equal to
    ///< `http_status` for codes with no `app::AppErrorCode` equivalent.
    unsigned legacy_rest_status;

    ///< The `error` string the existing REST resource routes emit today for
    ///< the equivalent `app::AppErrorCode` (from `codeForAppError`). Equal to
    ///< `name` for codes with no `app::AppErrorCode` equivalent.
    std::string_view legacy_rest_code;
};

/**
 * @brief Returns the single error table, one row per @ref CommandErrorCode in
 * enumerator order.
 *
 * The returned reference is to a function-local static and is valid for the
 * lifetime of the program. Row `i` describes the enumerator whose value is
 * `i`, so it can be indexed directly by `static_cast<std::size_t>(code)`.
 *
 * @return Reference to the immutable table; one entry per enumerator.
 */
const std::vector<error_info>& all_error_infos();

/**
 * @brief Looks up the table row for a given code.
 *
 * @param code Any @ref CommandErrorCode value. A value outside the enumerator
 * range (only producible by casting an arbitrary integer) is mapped to the
 * `internal` row rather than indexing out of bounds.
 * @return Reference to the matching row in @ref all_error_infos (or the
 * `internal` row for an out-of-range value); valid for the lifetime of the
 * program.
 */
const error_info& error_info_for(CommandErrorCode code);

/**
 * @brief Maps a wire `error.code` string back to its enumerator.
 *
 * @param name Candidate wire name; compared against each row's `name`.
 * @return The matching @ref CommandErrorCode, or `std::nullopt` when @p name
 * matches no row (including the empty string).
 */
std::optional<CommandErrorCode> error_code_from_name(std::string_view name);

/**
 * @brief Maps an application-layer `app::AppErrorCode` to its command code.
 *
 * Implemented as an exhaustive `switch` with no `default`, so adding an
 * `app::AppErrorCode` enumerator is a compile-time prompt to classify it here
 * (the same technique as `cli::ExitCodeForAppError`). Every same-named code
 * maps to itself, and `none` maps to `none`.
 *
 * @param code An application error code produced by `LinkCommandService`.
 * @return The corresponding @ref CommandErrorCode.
 */
CommandErrorCode from_app_error(app::AppErrorCode code);

/**
 * @brief Constructs a @ref CommandError from a code and message.
 *
 * @param code    The failure category.
 * @param message Fixed human text or an argument name; never an argument
 * value (milestone contract C6). Moved into the result unchanged.
 * @return A @ref CommandError carrying @p code and @p message verbatim.
 */
CommandError make_error(CommandErrorCode code, std::string message);

}  // namespace url_shortener::commands
