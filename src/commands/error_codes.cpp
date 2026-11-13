#include <url_shortener/commands/error_codes.hpp>

#include <cstddef>
#include <utility>

namespace url_shortener::commands
{

const std::vector<error_info>& all_error_infos()
{
    // Rows MUST appear in CommandErrorCode enumerator order so error_info_for
    // can index by static_cast<std::size_t>(code); the ordering is asserted by
    // the unit test, not re-verified at runtime. The legacy_rest_* columns are
    // copied verbatim from statusForAppError/codeForAppError in
    // src/http/handlers/link_handlers.cpp so the resource routes keep today's
    // bodies (contract C3); codes with no app::AppErrorCode equivalent repeat
    // the generic status/name there.
    static const std::vector<error_info> table = {
        {CommandErrorCode::none, "none", 200, 0, 200, "none"},
        {CommandErrorCode::invalid_envelope, "invalid_envelope", 400, 2, 400,
         "invalid_envelope"},
        {CommandErrorCode::unknown_command, "unknown_command", 404, 2, 404,
         "unknown_command"},
        {CommandErrorCode::invalid_field, "invalid_field", 400, 2, 400,
         "invalid_request"},
        {CommandErrorCode::invalid_url, "invalid_url", 400, 2, 400,
         "invalid_url"},
        {CommandErrorCode::invalid_slug, "invalid_slug", 400, 2, 400,
         "invalid_slug"},
        {CommandErrorCode::reserved_slug, "reserved_slug", 409, 2, 409,
         "reserved_slug"},
        {CommandErrorCode::slug_conflict, "slug_conflict", 409, 3, 409,
         "slug_conflict"},
        {CommandErrorCode::not_found, "not_found", 404, 1, 404, "not_found"},
        {CommandErrorCode::not_authorized, "not_authorized", 403, 5, 403,
         "not_authorized"},
        {CommandErrorCode::unsupported, "unsupported", 501, 2, 501,
         "unsupported"},
        {CommandErrorCode::busy, "busy", 503, 4, 503, "busy"},
        {CommandErrorCode::job_cancelled, "job_cancelled", 200, 4, 200,
         "job_cancelled"},
        {CommandErrorCode::storage_failure, "storage_failure", 500, 4, 500,
         "repository"},
        {CommandErrorCode::internal, "internal", 500, 4, 500,
         "internal_error"},
    };
    return table;
}

const error_info& error_info_for(CommandErrorCode code)
{
    const std::vector<error_info>& table = all_error_infos();
    const auto index = static_cast<std::size_t>(code);
    // Defensive: a value cast from an out-of-range integer must not index past
    // the table; report it as the least revealing failure instead.
    if (index >= table.size()) {
        return table[static_cast<std::size_t>(CommandErrorCode::internal)];
    }
    return table[index];
}

std::optional<CommandErrorCode> error_code_from_name(std::string_view name)
{
    // Linear scan: the table is tiny and bounded by the enum size.
    for (const error_info& info : all_error_infos())
    {
        if (info.name == name)
        {
            return info.code;
        }
    }
    return std::nullopt;
}

CommandErrorCode from_app_error(app::AppErrorCode code)
{
    // Exhaustive switch with no default: adding an app::AppErrorCode
    // enumerator is a compile-time prompt to classify it here (mirrors
    // cli::ExitCodeForAppError). Every same-named code maps to itself.
    switch (code)
    {
        case app::AppErrorCode::none:
            return CommandErrorCode::none;
        case app::AppErrorCode::invalid_url:
            return CommandErrorCode::invalid_url;
        case app::AppErrorCode::invalid_slug:
            return CommandErrorCode::invalid_slug;
        case app::AppErrorCode::reserved_slug:
            return CommandErrorCode::reserved_slug;
        case app::AppErrorCode::slug_conflict:
            return CommandErrorCode::slug_conflict;
        case app::AppErrorCode::invalid_field:
            return CommandErrorCode::invalid_field;
        case app::AppErrorCode::not_found:
            return CommandErrorCode::not_found;
        case app::AppErrorCode::storage_failure:
            return CommandErrorCode::storage_failure;
        case app::AppErrorCode::internal:
            return CommandErrorCode::internal;
    }
    // UNREACHABLE: AppErrorCode is a closed enum exhaustively handled above.
    return CommandErrorCode::internal;
}

CommandError make_error(CommandErrorCode code, std::string message)
{
    CommandError error;
    error.code = code;
    error.message = std::move(message);
    return error;
}

}  // namespace url_shortener::commands
