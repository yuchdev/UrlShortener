#pragma once

#include <string>
#include <vector>

#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{

/**
 * @file links_model.hpp
 * @brief The FTXUI-free model behind the Links tab (D3, subtask 06.0/12):
 * @ref LinksModel owns the D3 column set, the per-row action-availability rules
 * and the mapping from one `LinkView` JSON element to a table row.
 *
 * It belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no FTXUI, no
 * colour and no JSON tokenizing (it reads link JSON only through `json_view`,
 * contract C9). Authorization itself stays with the registry (contract C7); the
 * read-only availability rules here only decide client-side friction/disabling
 * (ADR 0003 rule 5, threat model T-116). Only `link.*` envelopes are
 * referenced; there is no `LinkView` C++ type in the TUI (constraint C8).
 */

/**
 * @brief One D3 action key's capability, independent of the command transport.
 *
 * Enumerator order is not contractual. The read actions (@ref detail,
 * @ref preview, @ref stats) are always available; the write actions are gated
 * by the session's read-only flag and, for the row-scoped ones, by the selected
 * link's status (see @ref LinksModel::availability).
 */
enum class LinkAction
{
    ///< Enter: open the selected link's detail view.
    detail,

    ///< `n`: start the new-link form (`link.create`).
    create,

    ///< `e`: edit the selected link (`link.update`).
    edit,

    ///< `t`: enable/disable the selected link (`link.enable` / `link.disable`).
    toggle,

    ///< `d`: soft-delete the selected link (`link.delete`).
    remove,

    ///< `u`: restore the selected soft-deleted link (`link.restore`).
    restore,

    ///< `p`: preview the selected link (`link.preview`).
    preview,

    ///< `s`: show the selected link's stats (its detail view).
    stats
};

/**
 * @brief Whether an action is available and, if not, why.
 *
 * A pure value type. When @ref enabled is `false`, @ref reason carries a short,
 * fixed-text explanation the screen surfaces in a toast (e.g. the read-only
 * notice or "restore the link first"); @ref reason is empty when @ref enabled
 * is `true`.
 */
struct ActionAvailability
{
    ///< `true` when the action may be invoked for the current
    ///< selection/session.
    bool enabled = false;

    ///< Fixed-text reason shown when @ref enabled is `false`; empty otherwise.
    std::string reason;
};

/**
 * @brief The D3 column set, the per-row action rules and the row mapping for
 * the Links tab (subtask 06.0/12).
 *
 * Built once per screen with the session's read-only flag. It is pure and
 * headless: every method is a deterministic function of its inputs and the
 * read-only flag, so the whole availability matrix and the row mapping are
 * unit-tested without a terminal (ADR 0003 rule 6).
 */
class LinksModel
{
public:
    /**
     * @brief Builds the model for a session.
     *
     * @param read_only `true` when the session runs as `ControlSet::User`
     * (`--read-only`): every write action (@ref LinkAction::create,
     * @ref LinkAction::edit, @ref LinkAction::toggle, @ref LinkAction::remove,
     * @ref LinkAction::restore) is reported unavailable with the read-only
     * reason, so no mutation leaves the screen even though the registry also
     * enforces it (defence in depth, threat model T-116).
     */
    explicit LinksModel(bool read_only);

    /**
     * @brief Decides whether an action is available for the selected link.
     *
     * Rules: the read actions (@ref LinkAction::detail, @ref
     * LinkAction::preview,
     * @ref LinkAction::stats) are always available. In a read-only session
     * every write action is unavailable with the read-only reason. Otherwise,
     * by the selected link's status: @ref LinkAction::toggle is available
     * unless the link is `deleted` ("restore the link first"); @ref
     * LinkAction::restore is available only for a `deleted` link; @ref
     * LinkAction::remove is available unless the link is already `deleted`;
     * @ref LinkAction::edit is available unless the link is `deleted` ("restore
     * the link first"); and
     * @ref LinkAction::create is always available (it does not depend on the
     * selection). An empty @p status_of_selected (no selection) disables every
     * row-scoped write action but still allows @ref LinkAction::create and the
     * read actions.
     *
     * @param action The action to classify.
     * @param status_of_selected The selected link's `status` string
     * (`"active"`,
     * `"disabled"`, `"expired"` or `"deleted"`), or empty when nothing is
     * selected.
     * @return The availability and, when disabled, the reason.
     */
    ActionAvailability availability(
        LinkAction action, const std::string& status_of_selected) const;

    /**
     * @brief The `link.*` command a toggle maps to for a given status.
     *
     * A `disabled` link toggles to `link.enable`; an `active` or `expired` link
     * (both enabled) toggles to `link.disable`; a `deleted` link (or any
     * unrecognized status) yields an empty string, since toggle is unavailable
     * there (see @ref availability).
     *
     * @param status_of_selected The selected link's `status` string.
     * @return `"link.enable"`, `"link.disable"`, or `""` when toggle does not
     * apply.
     */
    std::string toggle_command(const std::string& status_of_selected) const;

    /**
     * @brief The D3 columns in display order: SLUG, TARGET URL, STATUS, CLICKS,
     * EXPIRES.
     *
     * Each column's @ref ColumnDef::key is the `link.list` item field (or
     * dotted path) its cell reads: `slug`, `url`, `status`,
     * `stats.total_redirects`, `expires_at`. None is marked sortable - Links
     * disables the table view's built-in sort key so `s` is free for its
     * "stats" action - so a column key that is not an allowed server `sort`
     * value can never be emitted. CLICKS is right-aligned (numeric); EXPIRES
     * has no source key in the current `LinkView` JSON and renders `-` (a
     * documented D3 risk).
     *
     * @return The five D3 column definitions, in display order.
     */
    std::vector<ColumnDef> columns() const;

    /**
     * @brief The field keys (in @ref columns order) a row reads from a
     * `link.list` item, for @ref rows_from_result.
     *
     * @return `{"slug","url","status","stats.total_redirects","expires_at"}`.
     */
    std::vector<std::string> column_keys() const;

    /**
     * @brief Maps one `LinkView` JSON element to its D3 row cells.
     *
     * Resolves each @ref column_keys entry against @p link_json through the
     * sanctioned `json_view` accessors (contract C9): a string leaf becomes its
     * decoded text, an integer/boolean leaf its stringified value, and a
     * missing, null or container value renders as `-` (the D3 empty
     * convention). A dotted key (`stats.total_redirects`) is followed through
     * nested objects. The cells are raw server data; the view sanitizes them
     * before display (T-110).
     *
     * @param link_json One link element's exact JSON object text.
     * @return One cell per column, in @ref columns order; every column is
     * present (missing values are `-`), so the vector always has @ref columns
     * size.
     */
    std::vector<std::string> row_from_link_json(
        const std::string& link_json) const;

private:
    ///< `true` when every write action is reported unavailable (read-only
    ///< session).
    bool m_read_only_;
};

}  // namespace url_shortener::tui
