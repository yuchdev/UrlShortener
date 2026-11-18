#include "url_shortener/tui/confirm.hpp"

#include <utility>

#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

/// The reserved typed token for a (future) `migrate`-permission command; no
/// such command exists in Milestone 0006, so this branch is inert but kept so
/// the rule is covered (ADR 0003 rule 4).
constexpr char k_migrate_token[] = "MIGRATE";

}  // namespace

ConfirmPolicy::ConfirmPolicy(bool assume_yes) : m_assume_yes_(assume_yes) {}

ConfirmRequest ConfirmPolicy::decide(const CommandInfo& info,
                                     const std::string& args_json) const
{
    ConfirmRequest req;

    // --yes collapses every decision to "no dialog" (threat model T-117).
    if (m_assume_yes_)
    {
        return req;  // kind == none
    }

    // A non-mutating command never confirms.
    if (!info.mutating)
    {
        return req;  // kind == none
    }

    // Reserved rule: a migrate-permission command requires typing "MIGRATE".
    if (info.permission == "migrate")
    {
        req.kind = ConfirmKind::type_token;
        req.title = "Confirm migration";
        req.prompt =
            "This runs a migration. Type MIGRATE to confirm:";
        req.token = k_migrate_token;
        return req;
    }

    // link.delete requires typing the exact slug.
    if (info.name == "link.delete")
    {
        const std::optional<std::string> slug =
            get_string(args_json, "slug");
        if (slug.has_value())
        {
            req.kind = ConfirmKind::type_token;
            req.title = "Delete link";
            // The prompt DISPLAYS the sanitized slug (T-110) while the token
            // keeps the raw bytes to compare against.
            req.prompt = "Soft-delete \"" +
                         sanitize_for_terminal(*slug) +
                         "\"? Type the slug to confirm:";
            req.token = *slug;
            return req;
        }
        // No slug supplied: fall back to a plain confirm (the registry will
        // reject the call anyway).
    }

    // Every other mutating command gets a plain confirm.
    req.kind = ConfirmKind::yes_no;
    req.title = "Confirm";
    req.prompt = "Run \"" + sanitize_for_terminal(info.name) +
                 "\"? This changes data.";
    return req;
}

ConfirmModel::ConfirmModel(ConfirmRequest req) : m_request_(std::move(req)) {}

const ConfirmRequest& ConfirmModel::request() const
{
    return m_request_;
}

void ConfirmModel::set_input(std::string s)
{
    m_input_ = std::move(s);
}

const std::string& ConfirmModel::input() const
{
    return m_input_;
}

bool ConfirmModel::can_submit() const
{
    switch (m_request_.kind)
    {
    case ConfirmKind::type_token:
        // Compare against the RAW token bytes, never the displayed slug
        // (T-110): a bidi/zero-width slug must not be confirmable by typing its
        // sanitized form.
        return m_input_ == m_request_.token;
    case ConfirmKind::yes_no:
    case ConfirmKind::none:
        return true;
    }
    return true;
}

void ConfirmModel::submit()
{
    if (can_submit())
    {
        m_result_ = true;
    }
}

void ConfirmModel::cancel()
{
    m_result_ = false;
}

std::optional<bool> ConfirmModel::result() const
{
    return m_result_;
}

}  // namespace url_shortener::tui
