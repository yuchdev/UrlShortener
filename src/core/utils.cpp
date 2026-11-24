#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#endif

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{

namespace http = boost::beast::http;

std::time_t timegm_utc(std::tm* tm)
{
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

void gmtime_utc(const std::time_t* t, std::tm* tm)
{
#if defined(_WIN32)
    gmtime_s(tm, t);
#else
    gmtime_r(t, tm);
#endif
}

std::string currentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
    gmtime_utc(&now_time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string formatTimestamp(
    const std::chrono::system_clock::time_point time_point)
{
    const std::time_t ts = std::chrono::system_clock::to_time_t(time_point);
    std::tm tm {};
    gmtime_utc(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

namespace
{

/**
 * @brief Number of days in a Gregorian calendar month.
 * @param year Full (proleptic Gregorian) year, e.g. `2026`; used only to
 * decide whether February has 29 days.
 * @param month Month in the range `[1, 12]`; the caller must validate the
 * range before calling (values outside it index out of bounds).
 * @return Day count for the month, applying the Gregorian leap-year rule
 * (divisible by 4, except centuries not divisible by 400) for February.
 */
int days_in_month(int year, int month)
{
    static constexpr int kDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        const bool leap =
            (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

/**
 * @brief Consumes exactly @p width ASCII decimal digits.
 *
 * Reads @p width characters of @p s starting at @p pos. On success @p pos is
 * advanced past the digits; on failure @p pos is left unchanged.
 * @param s Full input string being scanned.
 * @param pos In/out cursor; the index of the first digit to read, advanced by
 * @p width on success.
 * @param width Exact number of digits required (e.g. `4` for a year).
 * @return The parsed non-negative integer, or `std::nullopt` if fewer than
 * @p width characters remain or any of them is not a `[0-9]` digit.
 */
std::optional<int> parse_fixed_digits(std::string_view s,
                                      std::size_t& pos,
                                      int width)
{
    if (pos + static_cast<std::size_t>(width) > s.size()) {
        return std::nullopt;
    }
    int v = 0;
    for (int i = 0; i < width; ++i) {
        const char c = s[pos + static_cast<std::size_t>(i)];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10 + (c - '0');
    }
    pos += static_cast<std::size_t>(width);
    return v;
}

}  // namespace

std::optional<std::chrono::system_clock::time_point> parseRfc3339Zulu(
    const std::string& value)
{
    const std::string_view s = value;
    std::size_t pos = 0;

    // Consumes a single expected literal byte, advancing the cursor.
    const auto expect = [&](char c) -> bool {
        if (pos >= s.size() || s[pos] != c) {
            return false;
        }
        ++pos;
        return true;
    };

    const auto year = parse_fixed_digits(s, pos, 4);
    if (!year || !expect('-')) {
        return std::nullopt;
    }
    const auto month = parse_fixed_digits(s, pos, 2);
    if (!month || !expect('-')) {
        return std::nullopt;
    }
    const auto day = parse_fixed_digits(s, pos, 2);
    if (!day || !expect('T')) {
        return std::nullopt;
    }
    const auto hour = parse_fixed_digits(s, pos, 2);
    if (!hour || !expect(':')) {
        return std::nullopt;
    }
    const auto minute = parse_fixed_digits(s, pos, 2);
    if (!minute || !expect(':')) {
        return std::nullopt;
    }
    const auto second = parse_fixed_digits(s, pos, 2);
    if (!second) {
        return std::nullopt;
    }

    // Optional fractional seconds: a single '.' then 1..9 decimal digits.
    // The sub-second component is parsed for validation but discarded, since
    // the result is floored to whole-second resolution via from_time_t.
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        const std::size_t frac_start = pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            ++pos;
        }
        const std::size_t frac_len = pos - frac_start;
        if (frac_len < 1 || frac_len > 9) {
            return std::nullopt;
        }
    }

    // A single uppercase 'Z' must terminate the timestamp; no numeric offset
    // (e.g. "+00:00") is accepted, and no trailing bytes may follow.
    if (!expect('Z') || pos != s.size()) {
        return std::nullopt;
    }

    // Calendar-range validation. Digit groups are non-negative by
    // construction, so only upper bounds (and the day-vs-month length) need
    // checking. Second 60 (a leap second) is deliberately rejected: the
    // broken-down-time conversion below cannot represent it unambiguously.
    if (*month < 1 || *month > 12) {
        return std::nullopt;
    }
    if (*day < 1 || *day > days_in_month(*year, *month)) {
        return std::nullopt;
    }
    if (*hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }

    std::tm tm {};
    tm.tm_year = *year - 1900;
    tm.tm_mon = *month - 1;
    tm.tm_mday = *day;
    tm.tm_hour = *hour;
    tm.tm_min = *minute;
    tm.tm_sec = *second;
    return std::chrono::system_clock::from_time_t(timegm_utc(&tm));
}

bool isExpired(const std::optional<std::string>& expires_at)
{
    if (!expires_at.has_value()) {
        return false;
    }
    const auto ts = parseRfc3339Zulu(*expires_at);
    if (!ts.has_value()) {
        return false;
    }
    return *ts <= std::chrono::system_clock::now();
}

std::string trim(const std::string& value)
{
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string clampString(const std::string& value, size_t max_len)
{
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len);
}

std::string hexEncode(const unsigned char* data, size_t len)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        out << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return out.str();
}

std::string generateSlug(const uint32_t length)
{
    static constexpr std::string_view chars =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::mt19937 rng {std::random_device {}()};
    std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

    std::string generated;
    generated.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        generated.push_back(chars[dist(rng)]);
    }
    return generated;
}

std::string generateId()
{
    return generateSlug(20);
}

bool isValidHttpUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool isValidSlug(const std::string& slug)
{
    static const std::regex slug_regex("^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$");
    if (!std::regex_match(slug, slug_regex)) {
        return false;
    }
    return true;
}

bool isReservedSlug(const std::string& slug)
{
    static const std::unordered_set<std::string> reserved = {"api",
                                                             "health",
                                                             "metrics",
                                                             "admin",
                                                             "login",
                                                             "logout",
                                                             "r",
                                                             "preview",
                                                             "stats"};
    std::string lower = slug;
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](const unsigned char c) { return std::tolower(c); });
    return reserved.find(lower) != reserved.end();
}

std::optional<std::pair<std::string, std::string>> splitUrl(
    const std::string& input_url)
{
    static const std::regex url_regex(
        R"(^([A-Za-z][A-Za-z0-9+.-]*):\/\/([^\/\?#]+)(.*)$)");
    std::smatch match;
    if (!std::regex_match(input_url, match, url_regex)) {
        return std::nullopt;
    }
    return std::make_pair(match[1].str(), match[2].str() + match[3].str());
}

namespace
{

/**
 * @brief Classifies a 32-bit IPv4 address (host byte order) as private/local.
 *
 * Mirrors the ranges @ref url_shortener::isPrivateHost already enforced by
 * string prefix - 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16,
 * 169.254.0.0/16 - plus the unspecified/this-network 0.0.0.0/8 block (the code
 * already treated the exact `0.0.0.0` as private; using /8 closes the
 * equivalent numeric encodings such as the decimal `0`). The check is a pure
 * bit test, so it is reachable from create/update validation only and adds no
 * work to the redirect fast path (contract C10).
 *
 * @param addr The address in host byte order (a.b.c.d -> (a<<24)|(b<<16)|
 *        (c<<8)|d).
 * @return `true` when @p addr falls in a private, loopback, link-local or
 *         unspecified range; `false` for a public address such as 8.8.8.8.
 */
bool is_private_ipv4(std::uint32_t addr)
{
    const std::uint32_t a = (addr >> 24) & 0xFFu;
    const std::uint32_t b = (addr >> 16) & 0xFFu;
    if (a == 127u || a == 10u || a == 0u) {
        return true;  // loopback / private / this-network
    }
    if (a == 172u && b >= 16u && b <= 31u) {
        return true;  // 172.16.0.0/12
    }
    if (a == 192u && b == 168u) {
        return true;  // 192.168.0.0/16
    }
    if (a == 169u && b == 254u) {
        return true;  // 169.254.0.0/16 link-local
    }
    // Special-use ranges that must not be a redirect target either (07.0/20
    // review): CGNAT, benchmarking, multicast, reserved and the broadcast
    // address. Public addresses (8.8.8.8, 1.1.1.1, 93.184.216.34) are in none.
    if (a == 100u && b >= 64u && b <= 127u) {
        return true;  // 100.64.0.0/10 carrier-grade NAT
    }
    if (a == 198u && (b == 18u || b == 19u)) {
        return true;  // 198.18.0.0/15 benchmarking
    }
    if (a >= 224u && a <= 239u) {
        return true;  // 224.0.0.0/4 multicast
    }
    if (a >= 240u) {
        return true;  // 240.0.0.0/4 reserved (covers 255.255.255.255)
    }
    return false;
}

/**
 * @brief Parses one `inet_aton`-style numeric IPv4 part (decimal/hex/octal).
 *
 * @param part The textual part (already lower-cased by the caller); non-empty.
 * @param value Receives the parsed value on success.
 * @return `true` when @p part is a well-formed `0x`-hex, leading-`0` octal, or
 *         decimal integer that fits in 64 bits; `false` for any other text
 *         (an empty part, a stray letter, a `0x` with no digits, an octal with
 *         an `8`/`9` digit), which makes the caller treat the host as a plain
 *         hostname rather than a numeric address.
 */
bool parse_ipv4_part(const std::string& part, std::uint64_t& value)
{
    if (part.empty()) {
        return false;
    }
    int base = 10;
    std::size_t start = 0;
    if (part.size() >= 2 && part[0] == '0' && (part[1] == 'x')) {
        base = 16;
        start = 2;
        if (part.size() == 2) {
            return false;  // "0x" with no digits
        }
    }
    else if (part.size() >= 2 && part[0] == '0') {
        base = 8;
        start = 1;
    }
    std::uint64_t acc = 0;
    for (std::size_t i = start; i < part.size(); ++i) {
        const char c = part[i];
        int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        }
        else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        }
        else {
            return false;  // non-digit for this base
        }
        if (digit >= base) {
            return false;  // e.g. '8'/'9' in an octal part
        }
        acc = acc * static_cast<std::uint64_t>(base)
            + static_cast<std::uint64_t>(digit);
        if (acc > 0xFFFFFFFFull) {
            return false;  // overflows an IPv4 address
        }
    }
    value = acc;
    return true;
}

/**
 * @brief Canonicalizes an `inet_aton`-style numeric IPv4 host to a 32-bit
 * address.
 *
 * Accepts 1-4 dot-separated parts, each decimal / `0x`-hex / leading-`0` octal,
 * with the usual short-form fill (1 part = whole 32-bit value, 2 parts =
 * a.(24-bit), 3 parts = a.b.(16-bit), 4 parts = a.b.c.d). This closes the SSRF
 * encodings (`2130706433`, `0x7f000001`, `0177.0.0.1`, `127.1`) that a
 * string-prefix check misses. A part that overflows its field, a `0x`/octal
 * with a bad digit, or a non-numeric part makes the whole host "not a numeric
 * IPv4" (returns `std::nullopt`), so a real hostname is never mis-parsed.
 *
 * @param host The host text (already lower-cased, brackets stripped).
 * @return The address in host byte order, or `std::nullopt` when @p host is not
 *         a well-formed numeric IPv4.
 */
std::optional<std::uint32_t> parse_ipv4_numeric(const std::string& host)
{
    if (host.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> parts;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t dot = host.find('.', begin);
        parts.push_back(host.substr(
            begin, dot == std::string::npos ? std::string::npos : dot - begin));
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
        if (parts.size() > 4) {
            return std::nullopt;  // never more than 4 parts
        }
    }
    if (parts.empty() || parts.size() > 4) {
        return std::nullopt;
    }

    std::vector<std::uint64_t> values(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!parse_ipv4_part(parts[i], values[i])) {
            return std::nullopt;
        }
    }

    const std::size_t n = values.size();
    // All but the last part are single octets; the last fills the remainder.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (values[i] > 0xFFull) {
            return std::nullopt;
        }
    }
    const unsigned remaining_bytes = static_cast<unsigned>(4 - (n - 1));
    const std::uint64_t last_max = remaining_bytes >= 4
        ? 0xFFFFFFFFull
        : ((1ull << (8u * remaining_bytes)) - 1ull);
    if (values[n - 1] > last_max) {
        return std::nullopt;
    }

    std::uint32_t addr = 0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        addr |= static_cast<std::uint32_t>(values[i])
            << (8u * (3u - static_cast<unsigned>(i)));
    }
    addr |= static_cast<std::uint32_t>(values[n - 1]);
    return addr;
}

/**
 * @brief Classifies a textual IPv6 address, if valid, as private/local.
 *
 * Parses @p host with `inet_pton(AF_INET6, ...)` (portable, no new library;
 * handles `::` compression and Windows), then matches loopback `::1`,
 * unspecified `::`, unique-local `fc00::/7`, link-local `fe80::/10`, and an
 * IPv4-mapped `::ffff:a.b.c.d` whose embedded IPv4 is re-checked with
 * @ref is_private_ipv4.
 *
 * @param host The host text with any surrounding brackets already stripped.
 * @return `std::nullopt` when @p host is not a valid IPv6 literal; otherwise
 *         `true`/`false` for whether that valid IPv6 address is private/local.
 */
std::optional<bool> classify_ipv6(const std::string& host)
{
    unsigned char buf[16];
    std::memset(buf, 0, sizeof(buf));
    if (::inet_pton(AF_INET6, host.c_str(), buf) != 1) {
        return std::nullopt;  // not a valid IPv6 literal
    }

    bool all_zero = true;
    for (unsigned char byte : buf) {
        if (byte != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return true;  // :: unspecified
    }

    bool loopback = buf[15] == 1;
    for (int i = 0; i < 15 && loopback; ++i) {
        loopback = buf[i] == 0;
    }
    if (loopback) {
        return true;  // ::1
    }

    if ((buf[0] & 0xFEu) == 0xFCu) {
        return true;  // fc00::/7 unique-local
    }
    if (buf[0] == 0xFEu && (buf[1] & 0xC0u) == 0x80u) {
        return true;  // fe80::/10 link-local
    }

    // Helper: fold a 4-byte window into a host-order IPv4 address.
    const auto embedded_v4 = [&buf](int offset) -> std::uint32_t
    {
        return (static_cast<std::uint32_t>(buf[offset]) << 24)
            | (static_cast<std::uint32_t>(buf[offset + 1]) << 16)
            | (static_cast<std::uint32_t>(buf[offset + 2]) << 8)
            | static_cast<std::uint32_t>(buf[offset + 3]);
    };

    // IPv4-mapped ::ffff:a.b.c.d -> re-check the embedded IPv4.
    bool mapped = buf[10] == 0xFFu && buf[11] == 0xFFu;
    for (int i = 0; i < 10 && mapped; ++i) {
        mapped = buf[i] == 0;
    }
    if (mapped) {
        return is_private_ipv4(embedded_v4(12));
    }

    // IPv4-compatible ::a.b.c.d (bytes 0..11 zero). :: and ::1 were already
    // handled above; any other address with a zero /96 prefix embeds a v4 in
    // the low 32 bits, so re-check it (07.0/20 review LOW, e.g. [::127.0.0.1]).
    bool compatible = true;
    for (int i = 0; i < 12 && compatible; ++i) {
        compatible = buf[i] == 0;
    }
    if (compatible) {
        return is_private_ipv4(embedded_v4(12));
    }

    // NAT64 64:ff9b::/96 -> the low 32 bits are the translated v4.
    bool nat64 = buf[0] == 0x00u && buf[1] == 0x64u && buf[2] == 0xFFu
        && buf[3] == 0x9Bu;
    for (int i = 4; i < 12 && nat64; ++i) {
        nat64 = buf[i] == 0;
    }
    if (nat64) {
        return is_private_ipv4(embedded_v4(12));
    }

    // 6to4 2002::/16 -> the embedded v4 is bytes 2..5.
    if (buf[0] == 0x20u && buf[1] == 0x02u) {
        return is_private_ipv4(embedded_v4(2));
    }

    return false;  // a valid but public IPv6 address
}

/**
 * @brief Strict allowlist for the URL host, before any private-target check.
 *
 * Fails closed (07.0/20 review): the host must be either a bracketed IPv6
 * literal (`[...]` whose interior is a valid `inet_pton` AF_INET6 address) or a
 * registered name / numeric IPv4 made only of ASCII letters, digits, `-` and
 * `.`, with at most ONE trailing `.`. Anything else - a `\`, whitespace, a
 * control byte, a non-ASCII byte, or a second trailing dot - is rejected, so a
 * host a browser would re-normalize (e.g. `2130706433\x` -> loopback,
 * `2130706433..` -> loopback) can never be stored. IDN hosts must arrive
 * already punycoded (`xn--...`); this function does not perform IDN/percent
 * decoding (the caller rejects a `%` in the host separately).
 *
 * @param host The already-lower-cased host (brackets kept for IPv6). Must be
 *        non-empty.
 * @return `true` when @p host is a valid literal under the rules above.
 */
bool is_valid_url_host(const std::string& host)
{
    if (host.empty()) {
        return false;
    }
    if (host.front() == '[') {
        if (host.size() < 3 || host.back() != ']') {
            return false;
        }
        const std::string inner = host.substr(1, host.size() - 2);
        unsigned char buf[16];
        return ::inet_pton(AF_INET6, inner.c_str(), buf) == 1;
    }
    // Registered name / numeric IPv4: ASCII letters, digits, '-' and '.' only.
    for (const unsigned char c : host) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!allowed) {
            return false;
        }
    }
    // At most one trailing '.' (a single FQDN dot); a second one is malformed.
    std::size_t trailing_dots = 0;
    for (std::size_t i = host.size(); i-- > 0 && host[i] == '.';) {
        ++trailing_dots;
    }
    return trailing_dots <= 1;
}

}  // namespace

bool isPrivateHost(const std::string& host)
{
    const std::string lower_host = [&host]()
    {
        std::string out = host;
        std::transform(out.begin(),
                       out.end(),
                       out.begin(),
                       [](const unsigned char c) { return std::tolower(c); });
        // Strip ONE trailing '.' (a fully-qualified-name dot, WHATWG "remove
        // last empty label") before any classification, so "localhost." and
        // "2130706433." / "0177.0.0.1." cannot slip past the hostname and
        // numeric checks (07.0/20 review). The IPv4 canonicalizer rejects an
        // empty last part, so without this a trailing dot would bypass it.
        if (out.size() > 1 && out.back() == '.') {
            out.pop_back();
        }
        return out;
    }();
    if (lower_host == "localhost" || lower_host == "::1" || lower_host == "::"
        || lower_host == "0.0.0.0" || lower_host == "[::1]"
        || lower_host == "[::]")
    {
        return true;
    }
    if (lower_host == "internal" || lower_host == "localhost.localdomain"
        || lower_host.find(".internal") != std::string::npos
        || lower_host.find(".local") != std::string::npos)
    {
        return true;
    }
    // Strip surrounding brackets from IPv6 addresses (e.g., "[fd00::1]" ->
    // "fd00::1") so that private/link-local prefix checks work correctly for
    // bracketed hosts.
    const std::string effective_host =
        (!lower_host.empty() && lower_host.front() == '['
         && lower_host.back() == ']')
        ? lower_host.substr(1, lower_host.size() - 2)
        : lower_host;

    // Canonicalize numeric hosts BEFORE the string-prefix checks so an
    // alternative IP encoding cannot bypass the private-target block (SSRF): a
    // decimal/hex/octal/short-form IPv4 (e.g. 2130706433, 0x7f000001,
    // 0177.0.0.1, 127.1) is reduced to its 32-bit address, and a (bracketed or
    // bare) IPv6 literal - including an IPv4-mapped ::ffff:a.b.c.d - is parsed
    // with inet_pton. A non-numeric hostname falls through unchanged (no DNS
    // resolution happens here).
    if (const std::optional<std::uint32_t> v4 =
            parse_ipv4_numeric(effective_host);
        v4.has_value())
    {
        return is_private_ipv4(*v4);
    }
    if (const std::optional<bool> v6 = classify_ipv6(effective_host);
        v6.has_value())
    {
        return *v6;
    }

    return effective_host == "localhost" || effective_host == "::1"
        || effective_host.rfind("127.", 0) == 0
        || effective_host.rfind("10.", 0) == 0
        || effective_host.rfind("192.168.", 0) == 0
        || effective_host.rfind("169.254.", 0) == 0
        || effective_host.rfind("172.16.", 0) == 0
        || effective_host.rfind("172.17.", 0) == 0
        || effective_host.rfind("172.18.", 0) == 0
        || effective_host.rfind("172.19.", 0) == 0
        || effective_host.rfind("172.20.", 0) == 0
        || effective_host.rfind("172.21.", 0) == 0
        || effective_host.rfind("172.22.", 0) == 0
        || effective_host.rfind("172.23.", 0) == 0
        || effective_host.rfind("172.24.", 0) == 0
        || effective_host.rfind("172.25.", 0) == 0
        || effective_host.rfind("172.26.", 0) == 0
        || effective_host.rfind("172.27.", 0) == 0
        || effective_host.rfind("172.28.", 0) == 0
        || effective_host.rfind("172.29.", 0) == 0
        || effective_host.rfind("172.30.", 0) == 0
        || effective_host.rfind("172.31.", 0) == 0
        || effective_host.rfind("fc", 0) == 0
        || effective_host.rfind("fd", 0) == 0
        || effective_host.rfind("fe80:", 0) == 0;
}

bool containsUnsafeUrlControlBytes(const std::string& value)
{
    const std::size_t size = value.size();
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        // C0 controls (NUL, TAB, CR, LF, ...) and DEL.
        if (byte <= 0x1F || byte == 0x7F) {
            return true;
        }
        // U+0085 NEL, UTF-8 encoded as C2 85.
        if (byte == 0xC2 && i + 1 < size
            && static_cast<unsigned char>(value[i + 1]) == 0x85)
        {
            return true;
        }
        // U+2028 / U+2029 line separators, UTF-8 encoded as E2 80 A8
        // and E2 80 A9.
        if (byte == 0xE2 && i + 2 < size
            && static_cast<unsigned char>(value[i + 1]) == 0x80)
        {
            const auto third = static_cast<unsigned char>(value[i + 2]);
            if (third == 0xA8 || third == 0xA9) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::string> normalizeTargetUrl(const std::string& input_url,
                                              const ServerConfig& config)
{
    // Reject line-terminator and control bytes before any parsing so a
    // crafted target (e.g. "http://evil\r\nSet-Cookie: x/p") can never be
    // stored and later written raw into a redirect Location header
    // (CWE-113 HTTP response splitting / CWE-93 CRLF injection).
    if (containsUnsafeUrlControlBytes(input_url)) {
        return std::nullopt;
    }
    const std::string trimmed = trim(input_url);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    const auto split = splitUrl(trimmed);
    if (!split.has_value()) {
        return std::nullopt;
    }

    auto [scheme, rest] = *split;
    std::transform(scheme.begin(),
                   scheme.end(),
                   scheme.begin(),
                   [](const unsigned char c) { return std::tolower(c); });
    if (scheme != "http" && scheme != "https") {
        return std::nullopt;
    }

    const auto host_end = rest.find_first_of("/?#");
    std::string authority =
        host_end == std::string::npos ? rest : rest.substr(0, host_end);
    const std::string suffix =
        host_end == std::string::npos ? "" : rest.substr(host_end);

    if (authority.find('@') != std::string::npos) {
        return std::nullopt;
    }

    // Refuse a backslash anywhere in the scheme/authority part (before the
    // path). WHATWG host parsing ends the host at '\' and a browser normalizes
    // '\' to '/', so "http://2130706433\x" would navigate to loopback; fail
    // closed instead of re-normalizing (07.0/20 review). A '\' inside the path
    // (in @ref suffix) is left untouched.
    if (authority.find('\\') != std::string::npos) {
        return std::nullopt;
    }

    // Split host from an optional port. A bracketed IPv6 authority must be
    // split at the closing ']' first, never at an inner ':', so "[::1]:8080"
    // yields host "[::1]" (not "[") and the private-target check actually sees
    // it (SSRF; otherwise [::1]/[fc00::1]/... bypass the block).
    std::string host;
    std::optional<std::string> port;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return std::nullopt;  // malformed bracketed authority
        }
        host = authority.substr(0, close + 1);  // keep the brackets: "[::1]"
        const std::string after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return std::nullopt;  // junk between ']' and the port
            }
            port = after.substr(1);
        }
    }
    else if (const auto colon = authority.find(':'); colon != std::string::npos)
    {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
    }
    else {
        host = authority;
    }
    std::transform(host.begin(),
                   host.end(),
                   host.begin(),
                   [](const unsigned char c) { return std::tolower(c); });
    if (host.empty()) {
        return std::nullopt;
    }
    // Percent-encoding in the host is not decoded here; a '%' is refused
    // outright (treated as an invalid URL). Decoding would reopen the
    // encoded-dot / encoded-IP bypass the private-target check exists to close
    // (e.g. "2130706433%2e" -> "2130706433." -> 127.0.0.1), so the host must be
    // literal before classification (07.0/20 review, WHATWG host parsing).
    if (host.find('%') != std::string::npos) {
        return std::nullopt;
    }
    // Strict host allowlist, fail closed (07.0/20 review). A host that is not a
    // valid bracketed IPv6 literal or an ASCII letter/digit/`-`/`.` registered
    // name (at most one trailing dot) is rejected in BOTH modes - a malformed
    // host such as one carrying whitespace or a second trailing dot is an
    // invalid URL regardless of `allow_private_targets`.
    if (!is_valid_url_host(host)) {
        return std::nullopt;
    }
    if (!config.shortener_allow_private_targets && isPrivateHost(host)) {
        return std::nullopt;
    }

    if ((scheme == "http" && port == "80")
        || (scheme == "https" && port == "443"))
    {
        port.reset();
    }

    std::ostringstream out;
    out << scheme << "://" << host;
    if (port.has_value() && !port->empty()) {
        out << ':' << *port;
    }
    out << suffix;
    return out.str();
}

std::string normalizeAndValidateBaseDomain(const std::string& raw_domain)
{
    const std::string trimmed = trim(raw_domain);
    if (trimmed.empty()) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN must not be empty");
    }
    const auto split = splitUrl(trimmed);
    if (!split.has_value()) {
        throw std::runtime_error(
            "SHORTENER_BASE_DOMAIN must include http:// or https:// and a "
            "host");
    }
    auto [scheme, authority_and_rest] = *split;
    std::transform(scheme.begin(),
                   scheme.end(),
                   scheme.begin(),
                   [](const unsigned char c) { return std::tolower(c); });
    if (scheme != "http" && scheme != "https") {
        throw std::runtime_error(
            "SHORTENER_BASE_DOMAIN scheme must be http or https");
    }
    if (authority_and_rest.find('@') != std::string::npos) {
        throw std::runtime_error(
            "SHORTENER_BASE_DOMAIN must not include userinfo");
    }
    if (authority_and_rest.find_first_of("?#") != std::string::npos) {
        throw std::runtime_error(
            "SHORTENER_BASE_DOMAIN must not include path, query, or fragment");
    }
    const auto slash = authority_and_rest.find('/');
    if (slash != std::string::npos) {
        const bool trailing_slashes_only =
            std::all_of(authority_and_rest.begin()
                            + static_cast<std::string::difference_type>(slash),
                        authority_and_rest.end(),
                        [](char c) { return c == '/'; });
        if (!trailing_slashes_only) {
            throw std::runtime_error(
                "SHORTENER_BASE_DOMAIN must not include path, query, or "
                "fragment");
        }
        authority_and_rest.erase(slash);
    }
    if (authority_and_rest.empty()) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN host is required");
    }

    return scheme + "://" + authority_and_rest;
}

std::string hashClientIdentifier(const std::string& raw_client,
                                 const std::string& salt)
{
    unsigned int len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(salt.data()),
         static_cast<int>(salt.size()),
         reinterpret_cast<const unsigned char*>(raw_client.data()),
         raw_client.size(),
         digest,
         &len);
    return hexEncode(digest, len);
}

std::optional<std::string> headerString(
    const http::request<http::string_body>& req,
    const http::field field,
    const size_t max_len)
{
    if (!req.base().count(field)) {
        return std::nullopt;
    }
    return clampString(std::string(req.base().at(field)), max_len);
}

namespace
{

/**
 * @brief Appends a six-character `\uXXXX` escape for @p code_point.
 * @param out Output buffer to append to.
 * @param code_point Unicode scalar value in `0..0xFFFF` to escape.
 */
void appendUnicodeEscape(std::string& out, unsigned int code_point)
{
    static const char hex_digits[] = "0123456789abcdef";
    out += "\\u";
    out.push_back(hex_digits[(code_point >> 12) & 0xF]);
    out.push_back(hex_digits[(code_point >> 8) & 0xF]);
    out.push_back(hex_digits[(code_point >> 4) & 0xF]);
    out.push_back(hex_digits[code_point & 0xF]);
}

}  // namespace

std::string jsonEscape(const std::string& input)
{
    // UTF-8 code point replacement (U+FFFD) emitted for any invalid byte.
    static const char replacement[] = "\xEF\xBF\xBD";

    std::string out;
    out.reserve(input.size());

    const auto* const data =
        reinterpret_cast<const unsigned char*>(input.data());
    const std::size_t size = input.size();

    std::size_t i = 0;
    while (i < size) {
        const unsigned char c = data[i];

        // ASCII fast path: short escapes, then \u-escape controls and DEL.
        if (c < 0x80) {
            switch (c) {
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20 || c == 0x7F) {
                        appendUnicodeEscape(out, c);
                    }
                    else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
            ++i;
            continue;
        }

        // Multi-byte UTF-8: decode and validate the whole sequence. Any
        // malformed byte is replaced with U+FFFD and decoding resynchronizes
        // one byte later so a single bad byte never consumes valid bytes.
        unsigned int code_point = 0;
        std::size_t length = 0;
        if ((c & 0xE0) == 0xC0) {
            code_point = c & 0x1Fu;
            length = 2;
        }
        else if ((c & 0xF0) == 0xE0) {
            code_point = c & 0x0Fu;
            length = 3;
        }
        else if ((c & 0xF8) == 0xF0) {
            code_point = c & 0x07u;
            length = 4;
        }
        else {
            // Lone continuation byte (0x80..0xBF) or invalid lead (>= 0xF8).
            out += replacement;
            ++i;
            continue;
        }

        if (i + length > size) {
            // Truncated multi-byte tail at end of input.
            out += replacement;
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t k = 1; k < length; ++k) {
            const unsigned char continuation = data[i + k];
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code_point = (code_point << 6) | (continuation & 0x3Fu);
        }
        if (!valid) {
            out += replacement;
            ++i;
            continue;
        }

        const bool overlong = (length == 2 && code_point < 0x80)
            || (length == 3 && code_point < 0x800)
            || (length == 4 && code_point < 0x10000);
        const bool surrogate = (code_point >= 0xD800 && code_point <= 0xDFFF);
        const bool out_of_range = (code_point > 0x10FFFF);
        if (overlong || surrogate || out_of_range) {
            out += replacement;
            ++i;
            continue;
        }

        // Valid scalar value. Escape the JS line/paragraph separators;
        // otherwise copy the original bytes through unchanged.
        if (code_point == 0x2028 || code_point == 0x2029) {
            appendUnicodeEscape(out, code_point);
        }
        else {
            out.append(reinterpret_cast<const char*>(data + i), length);
        }
        i += length;
    }

    return out;
}

std::string jsonString(const std::string& value)
{
    return '"' + jsonEscape(value) + '"';
}

std::optional<std::string> extractJsonStringField(const std::string& body,
                                                  const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size()
           && std::isspace(static_cast<unsigned char>(body[colon_pos])))
    {
        ++colon_pos;
    }
    if (colon_pos >= body.size() || body[colon_pos] != '"') {
        return std::nullopt;
    }

    ++colon_pos;
    std::string value;
    bool escape_next = false;
    for (auto i = colon_pos; i < body.size(); ++i) {
        const char c = body[i];
        if (escape_next) {
            value.push_back(c);
            escape_next = false;
            continue;
        }
        if (c == '\\') {
            escape_next = true;
            continue;
        }
        if (c == '"') {
            return value;
        }
        value.push_back(c);
    }

    return std::nullopt;
}

std::optional<bool> extractJsonBoolField(const std::string& body,
                                         const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size()
           && std::isspace(static_cast<unsigned char>(body[colon_pos])))
    {
        ++colon_pos;
    }
    if (body.compare(colon_pos, 4, "true") == 0) {
        return true;
    }
    if (body.compare(colon_pos, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

bool hasJsonField(const std::string& body, const std::string& field)
{
    const auto key = '"' + field + '"';
    return body.find(key) != std::string::npos;
}

std::optional<std::string> extractJsonValueToken(const std::string& body,
                                                 const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size()
           && std::isspace(static_cast<unsigned char>(body[colon_pos])))
    {
        ++colon_pos;
    }
    if (colon_pos >= body.size()) {
        return std::nullopt;
    }

    if (body[colon_pos] == 'n' && body.compare(colon_pos, 4, "null") == 0) {
        return std::string("null");
    }

    if (body[colon_pos] == '"') {
        const auto value = extractJsonStringField(body.substr(key_pos), field);
        return value;
    }

    if (body[colon_pos] == '[' || body[colon_pos] == '{') {
        const char open = body[colon_pos];
        const char close = open == '[' ? ']' : '}';
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        for (size_t i = colon_pos; i < body.size(); ++i) {
            const char c = body[i];
            if (in_string) {
                if (escape) {
                    escape = false;
                    continue;
                }
                if (c == '\\') {
                    escape = true;
                    continue;
                }
                if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == open) {
                ++depth;
            }
            else if (c == close) {
                --depth;
                if (depth == 0) {
                    return body.substr(colon_pos, i - colon_pos + 1);
                }
            }
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::vector<std::string>> extractJsonStringArrayField(
    const std::string& body, const std::string& field)
{
    const auto token = extractJsonValueToken(body, field);
    if (!token.has_value()) {
        return std::nullopt;
    }
    if (*token == "null") {
        return std::vector<std::string> {};
    }
    const std::string& raw = *token;
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
        return std::nullopt;
    }

    std::vector<std::string> out;
    size_t i = 1;
    while (i < raw.size() - 1) {
        while (i < raw.size() - 1
               && std::isspace(static_cast<unsigned char>(raw[i])))
        {
            ++i;
        }
        if (i >= raw.size() - 1) {
            break;
        }
        if (raw[i] != '"') {
            return std::nullopt;
        }
        ++i;
        std::string value;
        bool escape = false;
        while (i < raw.size() - 1) {
            const char c = raw[i++];
            if (escape) {
                value.push_back(c);
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                break;
            }
            value.push_back(c);
        }
        out.push_back(value);
        while (i < raw.size() - 1
               && std::isspace(static_cast<unsigned char>(raw[i])))
        {
            ++i;
        }
        if (i < raw.size() - 1) {
            if (raw[i] != ',') {
                return std::nullopt;
            }
            ++i;
        }
    }
    return out;
}

std::optional<std::unordered_map<std::string, std::string>>
extractJsonFlatObjectStringField(const std::string& body,
                                 const std::string& field)
{
    const auto token = extractJsonValueToken(body, field);
    if (!token.has_value()) {
        return std::nullopt;
    }
    if (*token == "null") {
        return std::unordered_map<std::string, std::string> {};
    }

    const std::string& raw = *token;
    if (raw.size() < 2 || raw.front() != '{' || raw.back() != '}') {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> out;
    static const std::regex pair_regex(R"re("([^"]+)"\s*:\s*"([^"]*)")re");
    for (auto it = std::sregex_iterator(raw.begin(), raw.end(), pair_regex);
         it != std::sregex_iterator();
         ++it)
    {
        out[(*it)[1].str()] = (*it)[2].str();
    }

    const std::string without_pairs = std::regex_replace(raw, pair_regex, "");
    for (const char c : without_pairs) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '{' && c != '}'
            && c != ',')
        {
            return std::nullopt;
        }
    }

    return out;
}

std::optional<RedirectType> parseRedirectType(const std::string& value)
{
    if (value == "temporary") {
        return RedirectType::temporary;
    }
    if (value == "permanent") {
        return RedirectType::permanent;
    }
    return std::nullopt;
}

std::string redirectTypeToString(const RedirectType value)
{
    return value == RedirectType::permanent ? "permanent" : "temporary";
}

std::string linkStatusToString(const LinkStatus status)
{
    switch (status) {
        case LinkStatus::deleted:
            return "deleted";
        case LinkStatus::disabled:
            return "disabled";
        case LinkStatus::expired:
            return "expired";
        case LinkStatus::active:
        default:
            return "active";
    }
}

LinkStatus resolveLinkStatus(const Link& link)
{
    if (link.deleted_at.has_value()) {
        return LinkStatus::deleted;
    }
    if (!link.enabled) {
        return LinkStatus::disabled;
    }
    if (isExpired(link.expires_at)) {
        return LinkStatus::expired;
    }
    return LinkStatus::active;
}

boost::beast::http::status redirectStatusFor(const Link& link)
{
    return link.redirect_type == RedirectType::permanent
        ? boost::beast::http::status::moved_permanently
        : boost::beast::http::status::found;
}

// Validation and Guard hooks
// The tag, metadata and campaign limits enforced below (20 tags x 32 chars,
// 50 metadata entries, 64/512-char keys/values, 128-char campaign fields)
// are duplicated as read-path caps in src/app/link_record_codec.cpp - keep
// both in sync when changing them.
bool validateTags(std::vector<std::string>& tags)
{
    if (tags.size() > 20) {
        return false;
    }

    std::vector<std::string> normalized;
    normalized.reserve(tags.size());
    std::unordered_set<std::string> seen;

    for (const auto& raw_tag : tags) {
        std::string tag = trim(raw_tag);
        if (tag.empty()) {
            return false;
        }
        if (tag.size() > 32) {
            return false;
        }

        for (char c : tag) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != ':'
                && c != '-' && c != '_')
            {
                return false;
            }
        }

        if (seen.insert(tag).second) {
            normalized.push_back(tag);
        }
    }

    tags = std::move(normalized);
    return true;
}

bool validateMetadata(
    const std::unordered_map<std::string, std::string>& metadata)
{
    if (metadata.size() > 50) {
        return false;
    }
    for (const auto& [key, value] : metadata) {
        if (key.empty() || key.size() > 64 || value.size() > 512) {
            return false;
        }
    }
    return true;
}

static bool validateCampaignField(const std::optional<std::string>& field)
{
    return !field.has_value() || field->size() <= 128;
}

bool validateCampaign(const std::optional<Link::Campaign>& campaign)
{
    if (!campaign.has_value()) {
        return true;
    }
    const auto& c = *campaign;
    return validateCampaignField(c.name) && validateCampaignField(c.source)
        && validateCampaignField(c.medium) && validateCampaignField(c.term)
        && validateCampaignField(c.content) && validateCampaignField(c.id);
}

bool passwordGuardCheck(
    const Link&,
    const boost::beast::http::request<boost::beast::http::string_body>&)
{
    return true;
}

bool rateLimitGuardAllow(
    const Link&,
    const boost::beast::http::request<boost::beast::http::string_body>&)
{
    return true;
}

}  // namespace url_shortener
