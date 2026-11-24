#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Length of the well-formed UTF-8 sequence starting at `text[pos]`.
 *
 * Follows RFC 3629 / Unicode Table 3-7: rejects stray continuation bytes,
 * overlong encodings, UTF-16 surrogates (U+D800..U+DFFF), code points above
 * U+10FFFF and sequences truncated by the end of `text`.
 *
 * @param text Bytes being scanned.
 * @param pos Index of the candidate lead byte; must be `< text.size()`.
 * @return 1-4 for a well-formed sequence, 0 if the bytes at `pos` do not
 * start one.
 */
std::size_t valid_utf8_sequence_length(std::string_view text, std::size_t pos)
{
    const auto byte = [&](std::size_t k)
    { return static_cast<unsigned char>(text[pos + k]); };
    const auto in = [](unsigned char b, unsigned char lo, unsigned char hi)
    { return b >= lo && b <= hi; };

    const unsigned char b0 = byte(0);
    if (b0 < 0x80) {
        return 1;
    }

    std::size_t len = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (in(b0, 0xC2, 0xDF)) {
        len = 2;
    }
    else if (b0 == 0xE0) {
        len = 3;
        lo = 0xA0;  // no overlongs
    }
    else if (in(b0, 0xE1, 0xEC) || in(b0, 0xEE, 0xEF)) {
        len = 3;
    }
    else if (b0 == 0xED) {
        len = 3;
        hi = 0x9F;  // no surrogates
    }
    else if (b0 == 0xF0) {
        len = 4;
        lo = 0x90;  // no overlongs
    }
    else if (in(b0, 0xF1, 0xF3)) {
        len = 4;
    }
    else if (b0 == 0xF4) {
        len = 4;
        hi = 0x8F;  // nothing above U+10FFFF
    }
    else {
        return 0;  // 0x80..0xC1 or 0xF5..0xFF can never lead
    }

    if (pos + len > text.size()) {
        return 0;
    }
    if (!in(byte(1), lo, hi)) {
        return 0;
    }
    for (std::size_t k = 2; k < len; ++k) {
        if (!in(byte(k), 0x80, 0xBF)) {
            return 0;
        }
    }
    return len;
}

/**
 * @brief Whether a well-formed sequence encodes a control character.
 *
 * @param text Bytes being scanned.
 * @param pos Start of a sequence already validated by
 * `valid_utf8_sequence_length`.
 * @param len Its length (1-4).
 * @return `true` for C0 controls (including NUL), DEL (U+007F) and C1
 * controls (U+0080..U+009F); `false` otherwise.
 */
bool is_control_code_point(std::string_view text,
                           std::size_t pos,
                           std::size_t len)
{
    const auto b0 = static_cast<unsigned char>(text[pos]);
    if (len == 1) {
        return b0 < 0x20 || b0 == 0x7F;
    }
    // U+0080..U+009F encode as C2 80..C2 9F.
    return len == 2 && b0 == 0xC2
        && static_cast<unsigned char>(text[pos + 1]) <= 0x9F;
}

}  // namespace

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
        {CommandErrorCode::invalid_envelope,
         "invalid_envelope",
         400,
         2,
         400,
         "invalid_envelope"},
        {CommandErrorCode::unknown_command,
         "unknown_command",
         404,
         2,
         404,
         "unknown_command"},
        {CommandErrorCode::invalid_field,
         "invalid_field",
         400,
         2,
         400,
         "invalid_request"},
        {CommandErrorCode::invalid_url,
         "invalid_url",
         400,
         2,
         400,
         "invalid_url"},
        {CommandErrorCode::invalid_slug,
         "invalid_slug",
         400,
         2,
         400,
         "invalid_slug"},
        {CommandErrorCode::reserved_slug,
         "reserved_slug",
         409,
         2,
         409,
         "reserved_slug"},
        {CommandErrorCode::slug_conflict,
         "slug_conflict",
         409,
         3,
         409,
         "slug_conflict"},
        {CommandErrorCode::not_found, "not_found", 404, 1, 404, "not_found"},
        {CommandErrorCode::not_authorized,
         "not_authorized",
         403,
         5,
         403,
         "not_authorized"},
        {CommandErrorCode::unsupported,
         "unsupported",
         501,
         2,
         501,
         "unsupported"},
        {CommandErrorCode::busy, "busy", 503, 4, 503, "busy"},
        {CommandErrorCode::job_cancelled,
         "job_cancelled",
         200,
         4,
         200,
         "job_cancelled"},
        {CommandErrorCode::storage_failure,
         "storage_failure",
         500,
         4,
         500,
         "repository"},
        {CommandErrorCode::internal, "internal", 500, 4, 500, "internal_error"},
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
    for (const error_info& info : all_error_infos()) {
        if (info.name == name) {
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
    switch (code) {
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

std::string sanitize_error_message(std::string_view message)
{
    // C6/T-02/T-15 defense in depth: callers are required to pass fixed,
    // value-free text, but neutralize control characters so a message can
    // never inject a log line, and bound the length so no caller can smuggle
    // unbounded text into a response or audit field.
    static constexpr std::size_t max_len = 128;
    std::string out;
    out.reserve(std::min(message.size(), max_len));

    std::size_t i = 0;
    while (i < message.size() && out.size() < max_len) {
        const std::size_t len = valid_utf8_sequence_length(message, i);
        if (len == 0) {
            // Invalid or truncated UTF-8: neutralize one byte and resync.
            out.push_back('?');
            ++i;
            continue;
        }
        if (is_control_code_point(message, i, len)) {
            out.push_back('?');
            i += len;
            continue;
        }
        if (out.size() + len > max_len) {
            // Never split a multi-byte character at the length cap.
            break;
        }
        out.append(message.data() + i, len);
        i += len;
    }
    return out;
}

}  // namespace url_shortener::commands
