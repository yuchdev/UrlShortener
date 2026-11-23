#include "url_shortener/observability/log_fields.hpp"

#include <string>

namespace url_shortener::observability
{

std::string SanitizeField(const std::string& value,
                          const LoggingConfig& cfg,
                          bool pii)
{
    if (pii && cfg.redact_pii) {
        return "[REDACTED]";
    }

    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(value.size() < cfg.max_field_length ? value.size()
                                                     : cfg.max_field_length);

    for (const char raw : value) {
        const auto byte = static_cast<unsigned char>(raw);

        // Build the escaped representation of this single input byte. CR and
        // LF are the log-injection vectors (CWE-117): without escaping, an
        // attacker-controlled field could inject a newline and forge a
        // separate, well-formed log line. Other C0 control bytes and DEL are
        // rendered as "\xHH" so a field is always a single printable token.
        char buf[4];
        std::size_t piece_len = 0;
        if (byte == '\r') {
            buf[0] = '\\';
            buf[1] = 'r';
            piece_len = 2;
        }
        else if (byte == '\n') {
            buf[0] = '\\';
            buf[1] = 'n';
            piece_len = 2;
        }
        else if (byte < 0x20 || byte == 0x7F) {
            buf[0] = '\\';
            buf[1] = 'x';
            buf[2] = kHex[(byte >> 4) & 0x0F];
            buf[3] = kHex[byte & 0x0F];
            piece_len = 4;
        }
        else {
            buf[0] = raw;
            piece_len = 1;
        }

        // Append atomically: stop before the bound rather than emit a partial
        // escape sequence. This keeps the result at most cfg.max_field_length
        // bytes and guarantees a "\r"/"\n"/"\xHH" token is never cut in half.
        if (out.size() + piece_len > cfg.max_field_length) {
            break;
        }
        out.append(buf, piece_len);
    }

    return out;
}

}  // namespace url_shortener::observability
