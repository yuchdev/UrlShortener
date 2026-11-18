/**
 * @file settings_screen.cpp
 * @brief The read-only Settings screen (D13, subtask 06.0/21): a thin FTXUI
 * renderer over @ref url_shortener::tui::SettingsModel. It submits one
 * `system.info` call on entry / refresh through the injected `AsyncCaller` and
 * lays the result out per D13. Every server-derived string is run through
 * `sanitize_for_terminal` before width fitting (threat model T-110); the screen
 * renders only the deserialized struct and issues no other command (T-126).
 */
#include "url_shortener/tui/screens/settings_screen.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/settings_model.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

///< Inner content width inside the window at the 80-column target (two border
///< columns subtracted).
constexpr std::size_t k_inner_width = 78;

///< Width of the left "Backend" column.
constexpr std::size_t k_left_col = 33;

///< Width of the right "Shortener" column (`k_left_col + k_right_col ==
///< k_inner_width`).
constexpr std::size_t k_right_col = 45;

/// Renders a boolean as the literal word, so the mono theme conveys state by
/// text rather than by colour alone (ADR 0003 accessibility).
std::string bool_word(bool value)
{
    return value ? "true" : "false";
}

/**
 * @brief Builds one fixed-width "  <label>  <value>" cell.
 *
 * The value is sanitized (T-110) and width-fitted before the whole cell is
 * padded to @p col_width, so a hostile or oversized server string can neither
 * inject control bytes nor push the cell past its column.
 *
 * @param label The fixed, composition-owned field label.
 * @param value The raw server value; sanitized and truncated here.
 * @param label_width Columns reserved for the padded label.
 * @param value_width Columns the fitted value may occupy.
 * @param col_width Total column width the cell is padded to.
 * @return A @p col_width-wide display string with no terminal-control bytes.
 */
std::string field_cell(const std::string& label,
                       const std::string& value,
                       std::size_t label_width,
                       std::size_t value_width,
                       std::size_t col_width)
{
    const std::string safe =
        truncate_to_width(sanitize_for_terminal(value), value_width);
    const std::string cell = "  " + pad_to_width(label, label_width) + safe;
    return pad_to_width(cell, col_width);
}

/// The header row: `Version <v>` on the left and `Read-only session <yes|no>`
/// on the right. The version is server data (sanitized); the rest is fixed text.
ftxui::Element header_row(const SettingsModel& model, const Theme& theme)
{
    using namespace ftxui;
    const SettingsInfo& info = model.info();
    const std::string version =
        truncate_to_width(sanitize_for_terminal(info.version), 30);
    return hbox({
        text("Version  "),
        text(version) | to_decorator(theme.title),
        filler(),
        text("Read-only session  "),
        text(model.read_only() ? "yes" : "no")
            | to_decorator(model.read_only() ? theme.warning : theme.ok),
    });
}

/// The two parallel columns: Backend (metadata/cache/analytics/rate limit,
/// persistence) and Shortener (the nine `shortener` fields).
ftxui::Element columns_row(const SettingsModel& model, const Theme& theme)
{
    using namespace ftxui;
    const SettingsInfo& info = model.info();
    const ShortenerConfig& cfg = info.shortener;

    std::string metadata = info.metadata;
    if (info.persistent)
    {
        metadata += "  (persistent)";
    }

    const std::string expiry =
        cfg.default_expiry_seconds
            ? std::to_string(*cfg.default_expiry_seconds) + " s"
            : std::string("never");

    Element left = vbox({
        text(pad_to_width("Backend", k_left_col))
            | to_decorator(theme.header_row),
        text(field_cell("metadata", metadata, 11, 20, k_left_col)),
        text(field_cell("cache", info.cache, 11, 20, k_left_col)),
        text(field_cell("analytics", info.analytics, 11, 20, k_left_col)),
        text(field_cell("rate limit", info.rate_limit, 11, 20, k_left_col)),
    });

    Element right = vbox({
        text(pad_to_width("Shortener", k_right_col))
            | to_decorator(theme.header_row),
        text(field_cell("base domain", cfg.base_domain, 22, 21, k_right_col)),
        text(field_cell("generated slug length",
                        std::to_string(cfg.generated_slug_length), 22, 21,
                        k_right_col)),
        text(field_cell("default redirect type", cfg.default_redirect_type, 22,
                        21, k_right_col)),
        text(field_cell("default expiry", expiry, 22, 21, k_right_col)),
        text(field_cell("allow private targets",
                        bool_word(cfg.allow_private_targets), 22, 21,
                        k_right_col)),
        text(field_cell("analytics enabled", bool_word(cfg.analytics_enabled),
                        22, 21, k_right_col)),
    });

    return hbox({std::move(left), std::move(right)});
}

/// The request-hardening block: the three limit fields from the `shortener`
/// object, laid out two-up then one full-width row.
ftxui::Element hardening_rows(const SettingsModel& model, const Theme& theme)
{
    using namespace ftxui;
    const ShortenerConfig& cfg = model.info().shortener;
    const std::string body = std::to_string(cfg.max_request_body_bytes)
        + " bytes";
    const std::string target = std::to_string(cfg.max_request_target_length)
        + " chars";
    const std::string id_len = std::to_string(cfg.request_id_max_length)
        + " chars";

    return vbox({
        text(pad_to_width("Request hardening", k_inner_width))
            | to_decorator(theme.header_row),
        text(field_cell("max request body", body, 22, 14, 40)
             + field_cell("max request target", target, 20, 14, 38)),
        text(field_cell("max request id length", id_len, 24, 20,
                        k_inner_width)),
    });
}

/// The ready body: header, the two columns, the hardening block and the fixed
/// read-only reminder footer (per the D13 sketch).
ftxui::Element ready_body(const SettingsModel& model, const Theme& theme)
{
    using namespace ftxui;
    return vbox({
        header_row(model, theme),
        text(""),
        columns_row(model, theme),
        text(""),
        hardening_rows(model, theme),
        filler(),
        separator(),
        text("Read-only view - change these with CLI flags / env vars and "
             "restart;"),
        text("no settings.set command exists (ADR 0002 / ADR 0003 Non-goals)."),
    });
}

/// The unavailable body: the single panel's error message and request id, with
/// the retry hint. Message and id are server-derived and sanitized.
ftxui::Element unavailable_body(const SettingsModel& model, const Theme& theme)
{
    using namespace ftxui;
    const std::string message = truncate_to_width(
        sanitize_for_terminal(model.error_message()), k_inner_width - 2);
    const std::string request_id = truncate_to_width(
        sanitize_for_terminal(model.error_request_id()), k_inner_width - 14);
    return vbox({
        filler(),
        hbox({filler(), text("Settings unavailable")
                            | to_decorator(theme.error),
              filler()}),
        text(""),
        hbox({filler(), text(message), filler()}),
        hbox({filler(), text("request id: " + request_id), filler()}),
        text(""),
        hbox({filler(), text("Press r to retry.") | to_decorator(theme.hint),
              filler()}),
        filler(),
    });
}

/// The loading body shown before the first response arrives.
ftxui::Element loading_body(const Theme& theme)
{
    using namespace ftxui;
    return vbox({filler(),
                 hbox({filler(),
                       text("Loading settings...") | to_decorator(theme.hint),
                       filler()}),
                 filler()});
}

/**
 * @brief The Settings screen (D13): a read-only view over `system.info`.
 *
 * Owns a @ref SettingsModel, submits one `system.info` call on @ref on_enter
 * and on `r`, and renders the result inside a titled window (folded to ASCII in
 * the mono theme). Holds the @ref ScreenContext by reference for the caller and
 * theme.
 */
class SettingsScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context.
     *
     * @param context The screen context; its `caller` and router are references
     * owned by @ref App that must outlive this screen (captured by reference).
     */
    explicit SettingsScreen(ScreenContext& context) : m_context_(context)
    {
        m_component_ = ftxui::CatchEvent(
            ftxui::Renderer([this] { return render(); }),
            [this](const ftxui::Event& event) {
                if (event == ftxui::Event::Character("r"))
                {
                    refresh();
                    return true;
                }
                return false;
            });
    }

    ScreenId id() const override { return ScreenId::settings; }

    ftxui::Component component() override { return m_component_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"r", "refresh"}, KeyHint {"q", "back"}};
    }

    void on_enter() override { refresh(); }

private:
    /// Submits the one `system.info` call, cancelling any still in flight so a
    /// stale response can never overwrite a newer one.
    void refresh()
    {
        if (m_token_ != 0)
        {
            m_context_.caller.cancel(m_token_);
        }
        // The handler captures `this`: the screen outlives the AsyncCaller
        // (App destroys the caller before its screens, see App's teardown
        // order), and the AsyncCaller suppresses completions once destroyed
        // (T-120), so the raw capture cannot run against a dead screen.
        m_token_ = m_context_.caller.submit(
            m_model_.refresh_request([this] { return m_ids_.next(); }),
            [this](const commands::CommandResponse& response) {
                m_model_.apply(response);
                m_token_ = 0;
            });
    }

    /// Builds the full window element for the current state, folding box glyphs
    /// to ASCII in the mono theme.
    ftxui::Element render() const
    {
        using namespace ftxui;
        const Theme& theme = theme_for(m_context_.options.theme);

        Element body;
        switch (m_model_.state())
        {
        case PanelState::ready:
            body = ready_body(m_model_, theme);
            break;
        case PanelState::unavailable:
            body = unavailable_body(m_model_, theme);
            break;
        case PanelState::loading:
        default:
            body = loading_body(theme);
            break;
        }

        Element win = window(text(" Settings "), std::move(body));
        if (theme.ascii_only)
        {
            win = ascii_filter(std::move(win));
        }
        return win;
    }

    ///< Screen context (caller, session, theme/options); not owned.
    ScreenContext& m_context_;

    ///< The read-only view-model; mutated by the caller handler on the UI
    ///< thread.
    SettingsModel m_model_;

    ///< Source of correlation ids for the `system.info` calls this screen makes.
    RequestIdGenerator m_ids_;

    ///< Token of the in-flight `system.info` call; `0` when none is pending.
    CallToken m_token_ = 0;

    ///< The cached root component (a Renderer wrapped to catch `r`).
    ftxui::Component m_component_;
};

}  // namespace

std::unique_ptr<IScreen> make_settings_screen(ScreenContext& context)
{
    return std::make_unique<SettingsScreen>(context);
}

}  // namespace url_shortener::tui
