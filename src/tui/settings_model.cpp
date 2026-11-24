/**
 * @file settings_model.cpp
 * @brief The read-only Settings view-model (D13, subtask 06.0/21). Deserializes
 * the `system.info` result into a TUI-local @ref
 * url_shortener::tui::SettingsInfo via `json_view` (no JSON tokenizing here,
 * contract C9) and tracks the single panel's load state. It issues only
 * `system.info` and holds no secret-handling logic of its own (threat model
 * T-126).
 */
#include "url_shortener/tui/settings_model.hpp"

#include <cstdint>
#include <limits>
#include <string>

#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/**
 * @brief Reads a non-negative JSON integer field as a `std::uint32_t`.
 *
 * @param json A JSON object text to read.
 * @param field The field name to look up.
 * @return The value clamped into the `std::uint32_t` range when the field is a
 * non-negative JSON integer; `std::nullopt` when the field is absent, `null`,
 * not an integer, or negative (so a malformed value leaves the member at its
 * default rather than wrapping).
 */
std::optional<std::uint32_t> get_u32(const std::string& json,
                                     const std::string& field)
{
    const std::optional<std::int64_t> value = get_int(json, field);
    if (!value || *value < 0)
    {
        return std::nullopt;
    }
    std::int64_t clamped = *value;
    constexpr std::int64_t max_u32 = std::numeric_limits<std::uint32_t>::max();
    if (clamped > max_u32)
    {
        clamped = max_u32;
    }
    return static_cast<std::uint32_t>(clamped);
}

}  // namespace

commands::CommandRequest SettingsModel::refresh_request(
    const std::function<std::string()>& next_id) const
{
    commands::CommandRequest request;
    request.command = "system.info";
    request.id = next_id ? next_id() : std::string();
    request.args_json = "{}";
    return request;
}

void SettingsModel::apply(const commands::CommandResponse& resp)
{
    if (!resp.ok)
    {
        m_state_ = PanelState::unavailable;
        m_info_ = SettingsInfo {};
        m_read_only_ = false;
        m_error_message_ = resp.error.message;
        m_error_request_id_ = resp.id;
        return;
    }

    const std::string& json = resp.result_json;
    SettingsInfo info;

    if (std::optional<std::string> version = get_string(json, "version"))
    {
        info.version = *version;
    }

    if (std::optional<std::string> backend = raw_field(json, "backend"))
    {
        if (std::optional<std::string> value = get_string(*backend, "metadata"))
        {
            info.metadata = *value;
        }
        if (std::optional<std::string> value = get_string(*backend, "cache"))
        {
            info.cache = *value;
        }
        if (std::optional<std::string> value =
                get_string(*backend, "analytics"))
        {
            info.analytics = *value;
        }
        if (std::optional<std::string> value =
                get_string(*backend, "rate_limit"))
        {
            info.rate_limit = *value;
        }
    }

    if (std::optional<bool> persistent = get_bool(json, "persistent"))
    {
        info.persistent = *persistent;
    }

    if (std::optional<std::string> shortener = raw_field(json, "shortener"))
    {
        ShortenerConfig& cfg = info.shortener;
        if (std::optional<std::string> value =
                get_string(*shortener, "base_domain"))
        {
            cfg.base_domain = *value;
        }
        if (std::optional<std::uint32_t> value =
                get_u32(*shortener, "generated_slug_length"))
        {
            cfg.generated_slug_length = *value;
        }
        if (std::optional<std::string> value =
                get_string(*shortener, "default_redirect_type"))
        {
            cfg.default_redirect_type = *value;
        }
        // Absent or explicit JSON null both leave this nullopt -> "never".
        cfg.default_expiry_seconds =
            get_u32(*shortener, "default_expiry_seconds");
        if (std::optional<bool> value =
                get_bool(*shortener, "allow_private_targets"))
        {
            cfg.allow_private_targets = *value;
        }
        if (std::optional<bool> value =
                get_bool(*shortener, "analytics_enabled"))
        {
            cfg.analytics_enabled = *value;
        }
        if (std::optional<std::uint32_t> value =
                get_u32(*shortener, "max_request_body_bytes"))
        {
            cfg.max_request_body_bytes = *value;
        }
        if (std::optional<std::uint32_t> value =
                get_u32(*shortener, "max_request_target_length"))
        {
            cfg.max_request_target_length = *value;
        }
        if (std::optional<std::uint32_t> value =
                get_u32(*shortener, "request_id_max_length"))
        {
            cfg.request_id_max_length = *value;
        }
    }

    m_info_ = std::move(info);
    m_read_only_ = get_bool(json, "read_only").value_or(false);
    m_state_ = PanelState::ready;
    m_error_message_.clear();
    m_error_request_id_.clear();
}

}  // namespace url_shortener::tui
