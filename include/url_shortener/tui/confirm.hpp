#pragma once

#include <optional>
#include <string>

#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @file confirm.hpp
 * @brief The FTXUI-free model of the D10 confirm dialog: @ref ConfirmPolicy
 * (which kind of confirmation a mutation needs) and @ref ConfirmModel (the
 * dialog's state and the raw-byte typed-token comparison).
 *
 * Belongs to `url_shortener_tui_model` (ADR 0003 rule 6). `confirm_view.cpp`
 * renders a @ref ConfirmModel and is the T-110 sink that sanitizes displayed
 * text. The typed-token guard is split deliberately (threat model T-110): the
 * dialog *displays* `sanitize_for_terminal(slug)` (built into
 * @ref ConfirmRequest::prompt by @ref ConfirmPolicy::decide) but
 * @ref ConfirmModel::can_submit compares the operator's input against the
 * *raw* @ref ConfirmRequest::token bytes, so a bidi override or zero-width
 * char in a slug cannot make the shown slug differ from the bytes matched and
 * cause the wrong link to be deleted.
 */

/**
 * @brief The kind of confirmation a mutation requires (ADR 0003 rule 4).
 */
enum class ConfirmKind
{
    none,        ///< No confirmation; submit directly.
    yes_no,      ///< A plain confirm/cancel prompt.
    type_token,  ///< The operator must type an exact token (e.g. the slug).
};

/**
 * @brief The description of a confirmation to show, produced by
 * @ref ConfirmPolicy::decide.
 *
 * A pure value type. @ref title and @ref prompt are display strings already
 * carrying the sanitized form of any server value (e.g. the slug); @ref token
 * holds the *raw* bytes the operator must type for a @ref ConfirmKind::type_token
 * confirmation and is never shown.
 */
struct ConfirmRequest
{
    ///< Which confirmation flow to run; @ref ConfirmKind::none means "no
    ///< dialog, submit directly".
    ConfirmKind kind = ConfirmKind::none;

    ///< Dialog heading (e.g. `"Delete link"`); display text, already sanitized
    ///< where it embeds a server value.
    std::string title;

    ///< The prompt line (e.g. `Soft-delete "promo"? Type the slug to
    ///< confirm:`); display text that embeds `sanitize_for_terminal(slug)`.
    std::string prompt;

    ///< The exact *raw* bytes the operator must type to enable submission for a
    ///< @ref ConfirmKind::type_token confirmation; empty for the other kinds.
    ///< Compared byte-for-byte, never displayed (T-110).
    std::string token;
};

/**
 * @brief Decides which confirmation a command invocation needs (ADR 0003
 * rule 4).
 *
 * The policy is the single place that maps a command to its confirmation. It
 * never consults authorization (the registry remains the sole authority,
 * contract C7); it only decides friction.
 */
class ConfirmPolicy
{
public:
    /**
     * @brief Constructs the policy.
     *
     * @param assume_yes `true` for a `--yes` session, which collapses every
     * decision to @ref ConfirmKind::none (including the typed-slug guard for
     * `link.delete`, threat model T-117, a documented safety trade-off; a
     * read-only session never reaches a confirm).
     */
    explicit ConfirmPolicy(bool assume_yes);

    /**
     * @brief Decides the confirmation for one command invocation.
     *
     * Rules, in order: a `--yes` session is always @ref ConfirmKind::none; a
     * non-mutating command is @ref ConfirmKind::none; a command whose base
     * permission is `"migrate"` is @ref ConfirmKind::type_token with token
     * `"MIGRATE"` (a reserved rule - no such command exists in Milestone 0006,
     * so the branch is inert but covered by a unit test); `link.delete` is
     * @ref ConfirmKind::type_token with the raw `slug` from @p args_json as the
     * token (falling back to @ref ConfirmKind::yes_no when no `slug` is present,
     * since the registry will reject the call anyway); every other mutating
     * command is @ref ConfirmKind::yes_no. For a type-token slug the prompt
     * embeds `sanitize_for_terminal(slug)` while @ref ConfirmRequest::token
     * keeps the raw bytes.
     *
     * @param info The command being invoked; @ref CommandInfo::mutating,
     * @ref CommandInfo::name and @ref CommandInfo::permission drive the choice.
     * @param args_json The request `args` object text; read only to extract
     * `slug` for `link.delete` (via `json_view::get_string`).
     * @return The confirmation to show; @ref ConfirmKind::none when the mutation
     * may be submitted directly.
     */
    ConfirmRequest decide(const CommandInfo& info,
                          const std::string& args_json) const;

private:
    ///< `true` when `--yes` suppresses every confirmation.
    bool m_assume_yes_;
};

/**
 * @brief The state of an open confirm dialog and its submit/cancel decision.
 *
 * Holds the @ref ConfirmRequest, the operator's typed input (raw bytes), and
 * the eventual outcome. @ref can_submit compares input against the raw token
 * for a @ref ConfirmKind::type_token request, so the comparison is unaffected
 * by how the slug is displayed (T-110).
 */
class ConfirmModel
{
public:
    /**
     * @brief Opens a dialog for @p req.
     *
     * @param req The confirmation to run; moved in and kept for the dialog's
     * lifetime. A @ref ConfirmKind::none request is accepted (an immediately
     * submittable dialog) so callers need not special-case it.
     */
    explicit ConfirmModel(ConfirmRequest req);

    /**
     * @brief The request this dialog is confirming.
     * @return A reference to the held @ref ConfirmRequest.
     */
    const ConfirmRequest& request() const;

    /**
     * @brief Replaces the operator's typed input (raw bytes).
     * @param s The new input; stored verbatim and compared byte-for-byte by
     * @ref can_submit.
     */
    void set_input(std::string s);

    /**
     * @brief The operator's current typed input (raw bytes).
     * @return A reference to the stored input.
     */
    const std::string& input() const;

    /**
     * @brief Whether submission is currently allowed.
     *
     * @return For @ref ConfirmKind::none and @ref ConfirmKind::yes_no, always
     * `true`. For @ref ConfirmKind::type_token, `true` only when @ref input
     * equals @ref ConfirmRequest::token exactly (case-sensitive, byte-for-byte
     * against the raw token; an empty input, a trailing space, or a sanitized
     * form of a bidi/zero-width slug all compare unequal and return `false`).
     */
    bool can_submit() const;

    /**
     * @brief Confirms the dialog, if allowed.
     *
     * Sets @ref result to `true` only when @ref can_submit is `true`; otherwise
     * a no-op, so an Enter press cannot submit an unmatched token.
     */
    void submit();

    /**
     * @brief Cancels the dialog, setting @ref result to `false`.
     */
    void cancel();

    /**
     * @brief The dialog's decision.
     * @return `true` once submitted, `false` once cancelled, `std::nullopt`
     * while the dialog is still open.
     */
    std::optional<bool> result() const;

private:
    ///< The confirmation being run (title/prompt/token/kind).
    ConfirmRequest m_request_;

    ///< The operator's typed input, raw bytes, compared against the raw token.
    std::string m_input_;

    ///< The decision: unset while open, `true` on submit, `false` on cancel.
    std::optional<bool> m_result_;
};

}  // namespace url_shortener::tui
