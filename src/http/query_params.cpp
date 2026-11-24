#include <url_shortener/http/query_params.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace url_shortener
{
namespace http
{

namespace
{

/// The single fixed error token reported for any malformed query (threat model
/// T-73): it names no value, so it is safe to return and log (contract C6).
constexpr const char* k_malformed_query = "malformed_query";

/**
 * @brief Converts one ASCII hex digit to its value.
 *
 * @param c Candidate hex digit.
 * @param out Receives the 0..15 value on success; untouched otherwise.
 * @return `true` when @p c is `0-9`, `a-f` or `A-F`; `false` otherwise.
 */
bool hex_value(const char c, int* out)
{
    if (c >= '0' && c <= '9') {
        *out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = 10 + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = 10 + (c - 'A');
        return true;
    }
    return false;
}

}  // namespace

std::optional<std::string> percent_decode(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out.push_back(' ');
            continue;
        }
        if (c == '%') {
            // Need exactly two hex digits after the '%'; a truncated "%2" or a
            // trailing "%" is malformed.
            if (i + 2 >= in.size()) {
                return std::nullopt;
            }
            int hi = 0;
            int lo = 0;
            if (!hex_value(in[i + 1], &hi) || !hex_value(in[i + 2], &lo)) {
                return std::nullopt;
            }
            const int byte = (hi << 4) | lo;
            if (byte == 0) {
                // A decoded NUL would truncate downstream C-string handling.
                return std::nullopt;
            }
            out.push_back(static_cast<char>(byte));
            i += 2;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool parse_query_string(std::string_view query,
                        std::vector<query_param>* out,
                        std::string* error)
{
    out->clear();
    if (query.empty()) {
        return true;
    }

    std::size_t pos = 0;
    while (pos <= query.size()) {
        const std::size_t amp = query.find('&', pos);
        const std::size_t token_end =
            amp == std::string_view::npos ? query.size() : amp;
        const std::string_view token =
            query.substr(pos, token_end - pos);

        // Split on the first '='; no '=' means an empty value.
        const std::size_t eq = token.find('=');
        const std::string_view raw_name =
            eq == std::string_view::npos ? token : token.substr(0, eq);
        const std::string_view raw_value =
            eq == std::string_view::npos ? std::string_view {}
                                         : token.substr(eq + 1);

        // An empty name (`=x`, `&&`, a leading/trailing `&`) is malformed; the
        // endpoint is strict by design.
        if (raw_name.empty()) {
            *error = k_malformed_query;
            return false;
        }

        const std::optional<std::string> name = percent_decode(raw_name);
        if (!name.has_value() || name->empty()) {
            *error = k_malformed_query;
            return false;
        }
        const std::optional<std::string> value = percent_decode(raw_value);
        if (!value.has_value()) {
            *error = k_malformed_query;
            return false;
        }

        out->push_back(query_param {*name, *value});

        if (amp == std::string_view::npos) {
            break;
        }
        pos = amp + 1;
    }
    return true;
}

}  // namespace http
}  // namespace url_shortener
