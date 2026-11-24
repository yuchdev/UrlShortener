/**
 * @file link_detail_model.cpp
 * @brief Implements the FTXUI-free Link detail / stats model (D4, subtask
 * 06.0/13): request building, `link.get` / `link.stats` parsing through
 * `json_view`, and the D4 grid / stats series. See @ref link_detail_model.hpp.
 */
#include <array>
#include <chrono>
#include <cstddef>

#include "url_shortener/tui/link_detail_model.hpp"

#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// The placeholder for an absent field in the D4 grid.
constexpr char k_dash[] = "-";

/// Returns @p value, or @ref k_dash when it is empty.
std::string or_dash(const std::string& value)
{
    return value.empty() ? std::string(k_dash) : value;
}

/// Decodes one raw JSON token that is expected to be a string, reusing the
/// sanctioned scanner (no manual JSON tokenizing, contract C9): a string token
/// is wrapped into a one-field object and read back with @ref get_string; a
/// non-string token is returned verbatim.
std::string decode_json_string(const std::string& raw_token)
{
    if (!raw_token.empty() && raw_token.front() == '"') {
        const std::string wrapped = "{\"v\":" + raw_token + "}";
        return get_string(wrapped, "v").value_or(std::string());
    }
    return raw_token;
}

/// Joins the decoded elements of a raw JSON string array with `", "`.
std::string join_string_array(const std::string& raw_array)
{
    std::string out;
    bool first = true;
    for (const std::string& element : split_array(raw_array)) {
        const std::string decoded = decode_json_string(element);
        if (decoded.empty()) {
            continue;
        }
        if (!first) {
            out += ", ";
        }
        first = false;
        out += decoded;
    }
    return out;
}

/// Renders `k=v` metadata pairs (source order) joined with `", "`.
std::string render_metadata(const std::string& link_json)
{
    const std::optional<std::string> raw = raw_field(link_json, "metadata");
    if (!raw.has_value()) {
        return std::string();
    }
    std::string out;
    bool first = true;
    for (const JsonField& field : object_fields(*raw)) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += field.key;
        out += '=';
        out += decode_json_string(field.raw_value);
    }
    return out;
}

/// Renders the campaign object as `name / source / medium` (absent parts become
/// `-`); returns empty when the campaign is null/absent.
std::string render_campaign(const std::string& link_json)
{
    const std::optional<std::string> raw = raw_field(link_json, "campaign");
    if (!raw.has_value() || *raw == "null") {
        return std::string();
    }
    const std::string name = get_string(*raw, "name").value_or(std::string());
    const std::string source =
        get_string(*raw, "source").value_or(std::string());
    const std::string medium =
        get_string(*raw, "medium").value_or(std::string());
    if (name.empty() && source.empty() && medium.empty()) {
        return std::string();
    }
    return or_dash(name) + " / " + or_dash(source) + " / " + or_dash(medium);
}

/// A non-negative integer field read as unsigned (negatives clamp to 0).
std::uint64_t unsigned_field(const std::string& json, const std::string& key)
{
    const std::int64_t value = get_int(json, key).value_or(0);
    return value < 0 ? 0u : static_cast<std::uint64_t>(value);
}

}  // namespace

std::string format_count(std::uint64_t value)
{
    std::string digits = std::to_string(value);
    std::string out;
    const std::size_t count = digits.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0 && (count - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

std::string weekday_from_epoch(std::int64_t epoch_seconds)
{
    constexpr std::int64_t k_day = 86400;
    std::int64_t days = epoch_seconds / k_day;
    if (epoch_seconds % k_day != 0 && epoch_seconds < 0) {
        --days;  // floor division for pre-epoch instants
    }
    // Epoch day 0 (1970-01-01) is a Thursday; index 3 in a Monday-first week.
    const int index = static_cast<int>(((days + 3) % 7 + 7) % 7);
    static const std::array<const char*, 7> labels = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return labels[static_cast<std::size_t>(index)];
}

std::string sparkline(const std::vector<std::uint64_t>& values,
                      std::size_t width,
                      bool ascii)
{
    if (width == 0 || values.empty()) {
        return std::string();
    }
    static const std::array<const char*, 8> ascii_ramp = {
        " ", ".", ":", "-", "=", "+", "*", "#"};
    static const std::array<const char*, 8> block_ramp = {
        "\xe2\x96\x81",  // U+2581 lower one eighth block
        "\xe2\x96\x82",  // U+2582
        "\xe2\x96\x83",  // U+2583
        "\xe2\x96\x84",  // U+2584
        "\xe2\x96\x85",  // U+2585
        "\xe2\x96\x86",  // U+2586
        "\xe2\x96\x87",  // U+2587
        "\xe2\x96\x88"};  // U+2588 full block
    const std::array<const char*, 8>& ramp = ascii ? ascii_ramp : block_ramp;
    const int top_level = static_cast<int>(ramp.size()) - 1;

    std::uint64_t max_value = 0;
    for (const std::uint64_t value : values) {
        if (value > max_value) {
            max_value = value;
        }
    }

    const std::size_t count = values.size() < width ? values.size() : width;
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        int level = 0;
        if (max_value > 0) {
            const long double ratio = static_cast<long double>(values[i])
                / static_cast<long double>(max_value);
            level = static_cast<int>(ratio * top_level + 0.5L);
            if (level < 0) {
                level = 0;
            }
            if (level > top_level) {
                level = top_level;
            }
        }
        out += ramp[static_cast<std::size_t>(level)];
    }
    return out;
}

LinkDetailModel::LinkDetailModel(const IClock& clock)
    : m_clock_(clock)
{
}

commands::CommandRequest LinkDetailModel::get_request(
    const std::string& slug, const std::string& id) const
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = id;
    request.command = "link.get";
    request.args_json = "{\"slug\":" + url_shortener::jsonString(slug) + "}";
    return request;
}

commands::CommandRequest LinkDetailModel::stats_request(
    const std::string& slug) const
{
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 m_clock_.now().time_since_epoch())
                                 .count();
    const std::int64_t from = now - k_window_seconds;
    commands::CommandRequest request;
    request.v = 1;
    request.command = "link.stats";
    request.args_json = "{\"slug\":" + url_shortener::jsonString(slug)
        + ",\"from\":" + url_shortener::jsonString(std::to_string(from))
        + ",\"to\":" + url_shortener::jsonString(std::to_string(now))
        + ",\"bucket\":" + url_shortener::jsonString(m_bucket_) + "}";
    return request;
}

void LinkDetailModel::apply_link(const std::string& link_json)
{
    m_link_json_ = link_json;
    m_link_state_ = State::loaded;
    m_link_error_code_.clear();
    m_link_error_message_.clear();
}

void LinkDetailModel::apply_stats(const std::string& stats_json)
{
    m_stats_json_ = stats_json;
    m_stats_state_ = State::loaded;
    m_stats_error_code_.clear();
    m_stats_error_message_.clear();
}

void LinkDetailModel::apply_link_error(const std::string& code,
                                       const std::string& message)
{
    m_link_state_ = State::failed;
    m_link_error_code_ = code;
    m_link_error_message_ = message;
}

void LinkDetailModel::apply_stats_error(const std::string& code,
                                        const std::string& message)
{
    m_stats_state_ = State::failed;
    m_stats_error_code_ = code;
    m_stats_error_message_ = message;
}

std::string LinkDetailModel::slug() const
{
    return get_string(m_link_json_, "slug").value_or(std::string());
}

bool LinkDetailModel::is_deleted() const
{
    return get_string(m_link_json_, "status").value_or(std::string())
        == "deleted";
}

void LinkDetailModel::set_bucket(const std::string& bucket)
{
    if (!bucket.empty()) {
        m_bucket_ = bucket;
    }
}

std::vector<DetailRow> LinkDetailModel::rows() const
{
    std::vector<DetailRow> rows;
    if (m_link_state_ != State::loaded) {
        return rows;
    }
    const auto str = [this](const char* key)
    { return get_string(m_link_json_, key).value_or(std::string()); };

    const std::string tags =
        join_string_array(raw_field(m_link_json_, "tags").value_or("[]"));
    const std::string campaign = render_campaign(m_link_json_);
    const std::string metadata = render_metadata(m_link_json_);

    rows.push_back(
        {"id", or_dash(str("id")), "short url", or_dash(str("short_url"))});
    rows.push_back({"target", or_dash(str("url")), "", ""});
    rows.push_back({"status",
                    or_dash(str("status")),
                    "redirect",
                    or_dash(str("redirect_type"))});
    // expires_at is not part of the LinkView JSON (consistent with the D3
    // EXPIRES column), so it always renders as the absent placeholder.
    rows.push_back({"created", or_dash(str("created_at")), "expires", k_dash});
    rows.push_back({"tags", or_dash(tags), "campaign", or_dash(campaign)});
    rows.push_back({"metadata", or_dash(metadata), "", ""});
    return rows;
}

std::string LinkDetailModel::stats_header() const
{
    if (m_stats_state_ != State::loaded) {
        return "Stats (7d)  -";
    }
    const std::uint64_t attempts =
        unsigned_field(m_stats_json_, "total_attempts");
    const std::uint64_t success =
        unsigned_field(m_stats_json_, "successful_redirects");
    const std::string bucket =
        get_string(m_stats_json_, "bucket").value_or(m_bucket_);
    return "Stats (7d)  attempts " + format_count(attempts) + "  success "
        + format_count(success) + "  [bucket: " + bucket + "]";
}

std::vector<std::pair<std::string, std::uint64_t>> LinkDetailModel::day_bars()
    const
{
    std::vector<std::pair<std::string, std::uint64_t>> bars;
    if (m_stats_state_ != State::loaded) {
        return bars;
    }
    const std::optional<std::string> raw =
        raw_field(m_stats_json_, "time_buckets");
    if (!raw.has_value()) {
        return bars;
    }
    for (const std::string& element : split_array(*raw)) {
        if (bars.size() >= k_max_buckets) {
            break;  // bound server-derived series length (T-121)
        }
        const std::int64_t start = get_int(element, "bucket_start").value_or(0);
        const std::uint64_t count = unsigned_field(element, "count");
        bars.emplace_back(weekday_from_epoch(start), count);
    }
    return bars;
}

}  // namespace url_shortener::tui
