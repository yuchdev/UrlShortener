#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::tui
{

/**
 * @brief Load state of the single Settings panel (D13).
 *
 * This mirrors the `PanelState` of the Dashboard model (subtask 06.0/17): a
 * panel is @ref loading until its one `system.info` call answers, then
 * @ref ready on success or @ref unavailable on any error. Settings has exactly
 * one panel, so the Dashboard's "degrade only the failing panel" collapses to
 * "degrade the one panel there is".
 *
 * @note 06.0/17 (the Dashboard) is the canonical owner of this enum per the 21
 * spec ("do not redefine it"), but it is not committed at the time this screen
 * lands, so the definition lives here for now. When 17 lands, both screens must
 * share one definition (e.g. a common `panel_state.hpp`) to avoid an ODR clash;
 * until then no translation unit includes both model headers, so the duplicate
 * is latent, not active.
 */
enum class PanelState
{
    ///< The `system.info` call is in flight and no data is shown yet.
    loading,

    ///< The call succeeded; @ref SettingsModel::info is valid and rendered.
    ready,

    ///< The call failed; the screen shows the error message/request id instead.
    unavailable
};

/**
 * @brief The non-secret shortener/request-hardening settings the screen renders.
 *
 * A TUI-local mirror of the server's `app::ShortenerSettings` (03.0/17),
 * populated field-by-field from the `system.info` result's `shortener` object
 * via `json_view` (contract K7/C9). It is redeclared here rather than reusing
 * the server struct because the enforced C8 include-boundary test forbids any
 * include under `url_shortener/app/` inside the TUI tree; this struct adds no
 * field the server does not emit, so it leaks nothing beyond `system.info`'s own
 * allowlist (threat model T-126). Every string value is raw server data and is
 * sanitized at the render sink, never here.
 */
struct ShortenerConfig
{
    ///< `shortener.base_domain`: the configured base URL (e.g.
    ///< `http://localhost:8000`); a non-secret server string.
    std::string base_domain;

    ///< `shortener.generated_slug_length`: the length of a server-generated
    ///< slug.
    std::uint32_t generated_slug_length = 0;

    ///< `shortener.default_redirect_type`: the default redirect type token
    ///< (e.g. `"temporary"`).
    std::string default_redirect_type;

    ///< `shortener.default_expiry_seconds`: the default link lifetime in
    ///< seconds, or `std::nullopt` when the JSON value is `null` (rendered as
    ///< `never`).
    std::optional<std::uint32_t> default_expiry_seconds;

    ///< `shortener.allow_private_targets`: whether links may point at
    ///< private/loopback hosts (SSRF posture).
    bool allow_private_targets = false;

    ///< `shortener.analytics_enabled`: whether the redirect analytics pipeline
    ///< is on.
    bool analytics_enabled = false;

    ///< `shortener.max_request_body_bytes`: the request-body size cap.
    std::uint32_t max_request_body_bytes = 0;

    ///< `shortener.max_request_target_length`: the request-target length cap.
    std::uint32_t max_request_target_length = 0;

    ///< `shortener.request_id_max_length`: the accepted request-id length cap.
    std::uint32_t request_id_max_length = 0;
};

/**
 * @brief The server identity and shortener configuration the screen renders.
 *
 * A TUI-local mirror of the server's `app::SystemInfo` (03.0/17) minus the
 * top-level `read_only` flag (which @ref SettingsModel exposes separately,
 * matching the JSON shape where `read_only` sits beside `backend`/`shortener`
 * rather than inside either). Populated via `json_view` from the `system.info`
 * result; it is redeclared here (not reused from `app/`) for the same
 * include-boundary reason as @ref ShortenerConfig, and carries only non-secret,
 * server-allowlisted values (threat model T-126).
 */
struct SettingsInfo
{
    ///< `version`: the build version string.
    std::string version;

    ///< `backend.metadata`: the metadata backend name (e.g. `"sqlite"`).
    std::string metadata;

    ///< `backend.cache`: the cache backend name (e.g. `"redis"`, `"none"`).
    std::string cache;

    ///< `backend.analytics`: the analytics backend name (e.g. `"noop"`).
    std::string analytics;

    ///< `backend.rate_limit`: the rate-limit backend name, or `"disabled"`.
    std::string rate_limit;

    ///< `persistent`: `true` when the metadata backend survives a restart.
    bool persistent = false;

    ///< The `shortener` block's non-secret settings.
    ShortenerConfig shortener;
};

/**
 * @brief The read-only view-model backing the Settings screen (D13).
 *
 * Owns one @ref PanelState and one deserialized @ref SettingsInfo. It issues a
 * single command, `system.info`, with no arguments; it never constructs or can
 * reach a mutating command (the only command this screen's whole stack issues is
 * `system.info`, asserted at the command level). It holds no FTXUI type and does
 * no terminal sanitization: it stores raw server strings verbatim and the screen
 * sanitizes them at the render sink (threat model T-110).
 *
 * Thread-safety: not synchronized; it is mutated and read only on the UI thread
 * (the `AsyncCaller` handler and the renderer both run there).
 */
class SettingsModel
{
public:
    /**
     * @brief Builds the one request this screen ever issues: `system.info`.
     *
     * The request carries an empty `args` object (`"{}"`) and no `options`; its
     * `permission` in the catalog is `read` (the screen is read-only for every
     * role). Called on @ref ready-triggering entry and on each manual refresh.
     *
     * @param next_id A generator for a fresh, unique correlation id (e.g.
     * `RequestIdGenerator::next`); invoked exactly once to fill the request
     * `id`. Must not be empty.
     * @return A `system.info` request with no arguments and the generated id.
     */
    commands::CommandRequest refresh_request(
        const std::function<std::string()>& next_id) const;

    /**
     * @brief Folds a `system.info` response into the model's state.
     *
     * On a success response (`resp.ok`) it deserializes `resp.result_json` into
     * @ref info and @ref read_only, sets @ref state to @ref PanelState::ready,
     * and clears any prior error. On any failure response (`!resp.ok`, which
     * includes a worker exception the `AsyncCaller` turned into an `internal`
     * envelope) it sets @ref state to @ref PanelState::unavailable, resets
     * @ref info to a default instance, resets @ref read_only to `false`, and
     * records `resp.error.message` and `resp.id` for the screen to display.
     *
     * @param resp The response delivered on the UI thread; its result JSON is
     * read field-by-field via `json_view` (a missing or ill-typed field simply
     * leaves that member at its default).
     */
    void apply(const commands::CommandResponse& resp);

    /**
     * @brief Returns the current load state.
     * @return @ref PanelState::loading before the first response,
     * @ref PanelState::ready after a success, @ref PanelState::unavailable
     * after a failure.
     */
    PanelState state() const { return m_state_; }

    /**
     * @brief Returns the deserialized server identity and shortener config.
     * @return The populated @ref SettingsInfo when @ref state is
     * @ref PanelState::ready; a default-constructed instance otherwise.
     */
    const SettingsInfo& info() const { return m_info_; }

    /**
     * @brief Returns the session read-only flag the server reported.
     *
     * Taken from the response's own `read_only` field, not from the TUI
     * process's `--read-only` option; the two are expected to agree, but the
     * screen shows what the server reported (matching the D2/D4 pattern of
     * trusting the command result over local state).
     *
     * @return The last success response's `read_only` value; `false` before any
     * success and after any failure.
     */
    bool read_only() const { return m_read_only_; }

    /**
     * @brief Returns the error message to show in the @ref
     * PanelState::unavailable state.
     * @return `resp.error.message` from the last failure; empty otherwise. Raw
     * server text, sanitized by the screen before display.
     */
    const std::string& error_message() const { return m_error_message_; }

    /**
     * @brief Returns the correlation id of the failed call.
     * @return `resp.id` from the last failure; empty otherwise. Sanitized by the
     * screen before display.
     */
    const std::string& error_request_id() const
    {
        return m_error_request_id_;
    }

private:
    ///< Current load state; starts at @ref PanelState::loading.
    PanelState m_state_ = PanelState::loading;

    ///< The deserialized snapshot; valid only when @ref m_state_ is ready.
    SettingsInfo m_info_;

    ///< The server-reported read-only flag from the last success.
    bool m_read_only_ = false;

    ///< The last failure's error message (raw server text).
    std::string m_error_message_;

    ///< The last failure's correlation id.
    std::string m_error_request_id_;
};

}  // namespace url_shortener::tui
