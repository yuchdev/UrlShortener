#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

/**
 * @brief Tests whether `c` is an RFC 3986 unreserved byte that
 * `percent_encode` emits verbatim.
 * @param c Byte to classify.
 * @return `true` for `[A-Za-z0-9-_.~]`; `false` otherwise.
 */
bool is_unreserved(const unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
        || c == '~';
}

/**
 * @brief Converts a single hex digit to its value.
 * @param c ASCII hex digit (`0-9`, `a-f`, `A-F`).
 * @return The value `0..15`, or `-1` if `c` is not a hex digit.
 */
int hex_value(const unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Splits `in` into fields on `sep`.
 *
 * Produces exactly `count(sep) + 1` fields, so `""` yields one empty field
 * and a trailing separator yields a trailing empty field. This preserves the
 * distinction between an N-element and an (N+1)-element list that the
 * attribute encodings rely on.
 * @param in Input to split.
 * @param sep Separator byte.
 * @return The fields in order.
 */
std::vector<std::string_view> split(const std::string_view in, const char sep)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = in.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(in.substr(start));
            break;
        }
        out.push_back(in.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

/**
 * @brief Parses a decimal `uint64_t`, rejecting anything non-numeric.
 * @param in Candidate digits.
 * @param out Receives the parsed value on success.
 * @return `true` if `in` is a non-empty run of ASCII digits that fits in a
 * `uint64_t`; `false` on an empty string, a non-digit byte, or overflow.
 */
bool parse_u64(const std::string_view in, std::uint64_t* out)
{
    if (in.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const unsigned char c : in) {
        if (c < '0' || c > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

/**
 * @brief Encodes a `Link::Stats` as the `attr_stats` value.
 * @param stats Counters to encode.
 * @return `total=N&r24=N&r7=N`, with a trailing `&last=pct(timestamp)` when
 * a last-access time is present.
 */
std::string encode_stats(const Link::Stats& stats)
{
    std::string out = "total=" + std::to_string(stats.total_redirects)
        + "&r24=" + std::to_string(stats.redirects_24h)
        + "&r7=" + std::to_string(stats.redirects_7d);
    if (stats.last_accessed_at.has_value()) {
        out += "&last=" + percent_encode(*stats.last_accessed_at);
    }
    return out;
}

/**
 * @brief Decodes an `attr_stats` value.
 *
 * Unknown keys are ignored; absent counters default to zero and an absent
 * `last` leaves `last_accessed_at` unset.
 * @param value Encoded stats string.
 * @param out Receives the decoded counters.
 * @return `true` on success; `false` if a counter is non-numeric or a
 * percent-escape in `last` is malformed.
 */
bool decode_stats(const std::string_view value, Link::Stats* out)
{
    Link::Stats parsed;
    for (const std::string_view field : split(value, '&')) {
        if (field.empty()) {
            continue;
        }
        const std::size_t eq = field.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = field.substr(0, eq);
        const std::string_view raw = field.substr(eq + 1);
        if (key == "total") {
            if (!parse_u64(raw, &parsed.total_redirects)) {
                return false;
            }
        }
        else if (key == "r24") {
            if (!parse_u64(raw, &parsed.redirects_24h)) {
                return false;
            }
        }
        else if (key == "r7") {
            if (!parse_u64(raw, &parsed.redirects_7d)) {
                return false;
            }
        }
        else if (key == "last") {
            auto decoded = percent_decode(raw);
            if (!decoded.has_value()) {
                return false;
            }
            parsed.last_accessed_at = std::move(*decoded);
        }
    }
    *out = std::move(parsed);
    return true;
}

/**
 * @brief Packs every `lnk_*` attribute of `link` into `attributes`.
 *
 * Writes only the reserved `lnk_*` keys; empty tags/metadata and an absent
 * campaign are omitted so they stay distinguishable from empty-valued ones.
 * @param link Link whose packable fields are encoded.
 * @param attributes Target map (cleared is the caller's responsibility; this
 * only inserts/overwrites `lnk_*` keys).
 */
void pack_attributes(const Link& link,
                     std::map<std::string, std::string>* attributes)
{
    auto& attrs = *attributes;
    attrs[std::string(attr_schema)] = "1";
    attrs[std::string(attr_enabled)] = link.enabled ? "1" : "0";
    attrs[std::string(attr_redirect)] =
        link.redirect_type == RedirectType::permanent ? "permanent"
                                                      : "temporary";

    if (!link.tags.empty()) {
        std::string value;
        for (std::size_t i = 0; i < link.tags.size(); ++i) {
            if (i != 0) {
                value += ',';
            }
            value += percent_encode(link.tags[i]);
        }
        attrs[std::string(attr_tags)] = std::move(value);
    }

    if (!link.metadata.empty()) {
        std::string value;
        bool first = true;
        for (const auto& [key, val] : link.metadata) {
            if (!first) {
                value += '&';
            }
            first = false;
            value += percent_encode(key);
            value += '=';
            value += percent_encode(val);
        }
        attrs[std::string(attr_metadata)] = std::move(value);
    }

    if (link.campaign.has_value()) {
        const Link::Campaign& c = *link.campaign;
        std::string value;
        const std::array<
            std::pair<std::string_view, const std::optional<std::string>&>,
            6>
            fields = {{{"name", c.name},
                       {"source", c.source},
                       {"medium", c.medium},
                       {"term", c.term},
                       {"content", c.content},
                       {"id", c.id}}};
        bool first = true;
        for (const auto& [field_name, field_value] : fields) {
            if (!field_value.has_value()) {
                continue;
            }
            if (!first) {
                value += '&';
            }
            first = false;
            value += field_name;
            value += '=';
            value += percent_encode(*field_value);
        }
        attrs[std::string(attr_campaign)] = std::move(value);
    }

    attrs[std::string(attr_stats)] = encode_stats(link.stats);

    if (link.deleted_at.has_value()) {
        attrs[std::string(attr_deleted)] = *link.deleted_at;
    }
}

/**
 * @brief Decodes the `attr_tags` value into a tag list.
 * @param value Encoded tags string.
 * @param out Receives the decoded tags.
 * @return `true` on success; `false` if any element has a malformed
 * percent-escape.
 */
bool decode_tags(const std::string_view value, std::vector<std::string>* out)
{
    out->clear();
    for (const std::string_view field : split(value, ',')) {
        auto decoded = percent_decode(field);
        if (!decoded.has_value()) {
            return false;
        }
        out->push_back(std::move(*decoded));
    }
    return true;
}

/**
 * @brief Decodes the `attr_metadata` value into a key-value map.
 *
 * A field without `=` is treated as a key with an empty value; empty fields
 * are skipped.
 * @param value Encoded metadata string.
 * @param out Receives the decoded map.
 * @return `true` on success; `false` if any key or value has a malformed
 * percent-escape.
 */
bool decode_metadata(const std::string_view value,
                     std::unordered_map<std::string, std::string>* out)
{
    out->clear();
    for (const std::string_view field : split(value, '&')) {
        if (field.empty()) {
            continue;
        }
        const std::size_t eq = field.find('=');
        const std::string_view raw_key =
            eq == std::string_view::npos ? field : field.substr(0, eq);
        const std::string_view raw_val = eq == std::string_view::npos
            ? std::string_view {}
            : field.substr(eq + 1);
        auto key = percent_decode(raw_key);
        auto val = percent_decode(raw_val);
        if (!key.has_value() || !val.has_value()) {
            return false;
        }
        (*out)[std::move(*key)] = std::move(*val);
    }
    return true;
}

/**
 * @brief Decodes the `attr_campaign` value into a `Link::Campaign`.
 *
 * Only the recognized field names are populated; unknown fields and fields
 * without `=` are ignored. The caller supplies a default-constructed
 * campaign, so an empty value yields an all-unset campaign.
 * @param value Encoded campaign string.
 * @param out Receives the decoded campaign.
 * @return `true` on success; `false` if any value has a malformed
 * percent-escape.
 */
bool decode_campaign(const std::string_view value, Link::Campaign* out)
{
    for (const std::string_view field : split(value, '&')) {
        if (field.empty()) {
            continue;
        }
        const std::size_t eq = field.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = field.substr(0, eq);
        auto decoded = percent_decode(field.substr(eq + 1));
        if (!decoded.has_value()) {
            return false;
        }
        if (key == "name") {
            out->name = std::move(*decoded);
        }
        else if (key == "source") {
            out->source = std::move(*decoded);
        }
        else if (key == "medium") {
            out->medium = std::move(*decoded);
        }
        else if (key == "term") {
            out->term = std::move(*decoded);
        }
        else if (key == "content") {
            out->content = std::move(*decoded);
        }
        else if (key == "id") {
            out->id = std::move(*decoded);
        }
    }
    return true;
}

/**
 * @brief Writes `message` to `*error` when `error` is non-null.
 * @param error Optional error out-param.
 * @param message Fixed error token to write.
 */
void set_error(std::string* error, const std::string_view message)
{
    if (error != nullptr) {
        *error = std::string(message);
    }
}

/**
 * @brief Looks up an attribute by key.
 * @param attributes Attribute map to search.
 * @param key Attribute key.
 * @return A pointer to the value, or `nullptr` if the key is absent.
 */
const std::string* find_attr(
    const std::map<std::string, std::string>& attributes,
    const std::string_view key)
{
    const auto it = attributes.find(std::string(key));
    return it == attributes.end() ? nullptr : &it->second;
}

}  // namespace

std::string percent_encode(const std::string_view in)
{
    static constexpr char k_hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (const unsigned char c : in) {
        if (is_unreserved(c)) {
            out.push_back(static_cast<char>(c));
        }
        else {
            out.push_back('%');
            out.push_back(k_hex[c >> 4]);
            out.push_back(k_hex[c & 0x0F]);
        }
    }
    return out;
}

std::optional<std::string> percent_decode(const std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c != '%') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (i + 2 >= in.size()) {
            return std::nullopt;
        }
        const int hi = hex_value(static_cast<unsigned char>(in[i + 1]));
        const int lo = hex_value(static_cast<unsigned char>(in[i + 2]));
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return out;
}

bool link_to_create_request(const Link& link,
                            ::CreateLinkRequest* out,
                            std::string* error)
{
    if (out == nullptr) {
        set_error(error, error_invalid_request);
        return false;
    }

    ::CreateLinkRequest request;
    request.short_code = link.slug;
    request.target_url = link.target_url;

    if (link.expires_at.has_value()) {
        const auto parsed = parseRfc3339Zulu(*link.expires_at);
        if (!parsed.has_value()) {
            set_error(error, error_bad_timestamp);
            return false;
        }
        request.expires_at = *parsed;
    }
    // A soft-delete timestamp, if any, must still parse even on the create
    // path so a round trip is lossless; it is carried only in `attributes`.
    if (link.deleted_at.has_value()
        && !parseRfc3339Zulu(*link.deleted_at).has_value())
    {
        set_error(error, error_bad_timestamp);
        return false;
    }

    request.owner = std::nullopt;
    request.is_active = link.enabled && !link.deleted_at.has_value();
    pack_attributes(link, &request.attributes);

    *out = std::move(request);
    return true;
}

bool link_to_record(const Link& link, ::LinkRecord* out, std::string* error)
{
    if (out == nullptr) {
        set_error(error, error_invalid_request);
        return false;
    }

    const auto created = parseRfc3339Zulu(link.created_at);
    const auto updated = parseRfc3339Zulu(link.updated_at);
    if (!created.has_value() || !updated.has_value()) {
        set_error(error, error_bad_timestamp);
        return false;
    }

    ::LinkRecord record;
    record.id = link.id;
    record.short_code = link.slug;
    record.target_url = link.target_url;
    record.created_at = *created;
    record.updated_at = *updated;

    if (link.expires_at.has_value()) {
        const auto parsed = parseRfc3339Zulu(*link.expires_at);
        if (!parsed.has_value()) {
            set_error(error, error_bad_timestamp);
            return false;
        }
        record.expires_at = *parsed;
    }
    if (link.deleted_at.has_value()) {
        const auto parsed = parseRfc3339Zulu(*link.deleted_at);
        if (!parsed.has_value()) {
            set_error(error, error_bad_timestamp);
            return false;
        }
        record.deleted_at = *parsed;
    }

    record.owner = std::nullopt;
    record.is_active = link.enabled && !link.deleted_at.has_value();
    pack_attributes(link, &record.attributes);

    *out = std::move(record);
    return true;
}

bool record_to_link(const ::LinkRecord& record, Link* out, std::string* error)
{
    if (out == nullptr) {
        set_error(error, error_invalid_request);
        return false;
    }

    // Fail closed on a schema version written by an incompatible codec
    // (threat model T-37): trusting the remaining fields could misread them.
    if (const std::string* schema = find_attr(record.attributes, attr_schema);
        schema != nullptr && *schema != "1")
    {
        set_error(error, error_bad_attribute);
        return false;
    }

    Link link;
    link.id = record.id;
    link.slug = record.short_code;
    link.target_url = record.target_url;
    link.created_at = formatTimestamp(record.created_at);
    link.updated_at = formatTimestamp(record.updated_at);
    if (record.expires_at.has_value()) {
        link.expires_at = formatTimestamp(*record.expires_at);
    }

    // enabled: prefer the explicit attribute, else fall back to is_active.
    if (const std::string* enabled = find_attr(record.attributes, attr_enabled);
        enabled != nullptr)
    {
        link.enabled = (*enabled == "1");
    }
    else {
        link.enabled = record.is_active;
    }

    link.redirect_type = RedirectType::temporary;
    if (const std::string* redirect =
            find_attr(record.attributes, attr_redirect);
        redirect != nullptr && *redirect == "permanent")
    {
        link.redirect_type = RedirectType::permanent;
    }

    if (const std::string* tags = find_attr(record.attributes, attr_tags);
        tags != nullptr)
    {
        if (!decode_tags(*tags, &link.tags)) {
            set_error(error, error_bad_attribute);
            return false;
        }
    }

    if (const std::string* meta = find_attr(record.attributes, attr_metadata);
        meta != nullptr)
    {
        if (!decode_metadata(*meta, &link.metadata)) {
            set_error(error, error_bad_attribute);
            return false;
        }
    }

    if (const std::string* campaign =
            find_attr(record.attributes, attr_campaign);
        campaign != nullptr)
    {
        Link::Campaign parsed;
        if (!decode_campaign(*campaign, &parsed)) {
            set_error(error, error_bad_attribute);
            return false;
        }
        link.campaign = std::move(parsed);
    }

    if (const std::string* stats = find_attr(record.attributes, attr_stats);
        stats != nullptr)
    {
        if (!decode_stats(*stats, &link.stats)) {
            set_error(error, error_bad_attribute);
            return false;
        }
    }

    // deleted_at: prefer the packed attribute over the (SQL-dropped) column.
    if (const std::string* deleted = find_attr(record.attributes, attr_deleted);
        deleted != nullptr)
    {
        link.deleted_at = *deleted;
    }
    else if (record.deleted_at.has_value()) {
        link.deleted_at = formatTimestamp(*record.deleted_at);
    }

    *out = std::move(link);
    return true;
}

void set_stats_attribute(std::map<std::string, std::string>* attributes,
                         const Link::Stats& stats)
{
    (*attributes)[std::string(attr_stats)] = encode_stats(stats);
}

std::optional<Link::Stats> get_stats_attribute(
    const std::map<std::string, std::string>& attributes)
{
    const std::string* stats = find_attr(attributes, attr_stats);
    if (stats == nullptr) {
        return std::nullopt;
    }
    Link::Stats out;
    if (!decode_stats(*stats, &out)) {
        return std::nullopt;
    }
    return out;
}

}  // namespace app
}  // namespace url_shortener
