#include "url_shortener/tui/field_parsers.hpp"

#include <cstddef>
#include <limits>
#include <string>

namespace url_shortener::tui
{

namespace
{

/// Removes leading and trailing ASCII spaces and tabs from @p s.
std::string trim_ascii(const std::string& s)
{
    const auto is_ws = [](char c) { return c == ' ' || c == '\t'; };
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_ws(s[begin])) {
        ++begin;
    }
    while (end > begin && is_ws(s[end - 1])) {
        --end;
    }
    return s.substr(begin, end - begin);
}

}  // namespace

std::vector<std::string> parse_string_list(const std::string& s)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = s.find(',', start);
        const std::string raw = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);
        const std::string item = trim_ascii(raw);
        if (!item.empty()) {
            out.push_back(item);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::optional<std::vector<std::pair<std::string, std::string>>> parse_string_map(
    const std::string& s)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = s.find(',', start);
        const std::string raw = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);
        const std::string entry = trim_ascii(raw);
        if (!entry.empty()) {
            const std::size_t eq = entry.find('=');
            if (eq == std::string::npos) {
                return std::nullopt;  // entry missing '='
            }
            const std::string key = trim_ascii(entry.substr(0, eq));
            const std::string value = trim_ascii(entry.substr(eq + 1));
            if (key.empty()) {
                return std::nullopt;  // empty key rejected
            }
            bool found = false;
            for (auto& pair : out) {
                if (pair.first == key) {
                    pair.second = value;  // last value wins, position kept
                    found = true;
                    break;
                }
            }
            if (!found) {
                out.emplace_back(key, value);
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::optional<long long> parse_integer(const std::string& s)
{
    const std::string t = trim_ascii(s);
    if (t.empty()) {
        return std::nullopt;
    }

    std::size_t i = 0;
    bool negative = false;
    if (t[0] == '+' || t[0] == '-') {
        negative = (t[0] == '-');
        i = 1;
    }
    if (i >= t.size()) {
        return std::nullopt;  // a bare sign with no digits
    }

    // Accumulate the magnitude unsigned so INT64_MIN is representable, and
    // reject anything that would exceed the signed range before it wraps.
    const unsigned long long limit = negative
        ? static_cast<unsigned long long>(
              std::numeric_limits<long long>::max())
            + 1ULL
        : static_cast<unsigned long long>(
              std::numeric_limits<long long>::max());
    unsigned long long acc = 0;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const unsigned long long digit = static_cast<unsigned long long>(c - '0');
        if (acc > (limit - digit) / 10ULL) {
            return std::nullopt;  // would overflow the signed range
        }
        acc = acc * 10ULL + digit;
    }

    if (negative) {
        if (acc == static_cast<unsigned long long>(
                std::numeric_limits<long long>::max())
                + 1ULL) {
            return std::numeric_limits<long long>::min();
        }
        return -static_cast<long long>(acc);
    }
    return static_cast<long long>(acc);
}

std::string format_string_list(const std::vector<std::string>& items,
                               bool& representable)
{
    representable = true;
    std::string out;
    bool first = true;
    for (const std::string& item : items) {
        if (item.find(',') != std::string::npos) {
            representable = false;
        }
        if (!first) {
            out += ", ";
        }
        first = false;
        out += item;
    }
    return out;
}

std::string format_string_map(
    const std::vector<std::pair<std::string, std::string>>& pairs,
    bool& representable)
{
    representable = true;
    std::string out;
    bool first = true;
    for (const auto& pair : pairs) {
        if (pair.first.find(',') != std::string::npos
            || pair.first.find('=') != std::string::npos
            || pair.second.find(',') != std::string::npos
            || pair.second.find('=') != std::string::npos) {
            representable = false;
        }
        if (!first) {
            out += ", ";
        }
        first = false;
        out += pair.first;
        out += '=';
        out += pair.second;
    }
    return out;
}

}  // namespace url_shortener::tui
