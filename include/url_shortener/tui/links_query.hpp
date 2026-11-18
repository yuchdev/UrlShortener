#pragma once

#include <string>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{

/**
 * @file links_query.hpp
 * @brief Pure, FTXUI-free query helpers for the Links tab (D3, subtask
 * 06.0/12): the D3 status radio choice, the parsed filter bar, and the exact
 * `link.list` request the @ref TableModel's query handler submits.
 *
 * This header belongs to `url_shortener_tui_model` (ADR 0003 rule 6): it holds
 * no FTXUI and no colour, builds the request `args` only through the shared
 * JSON escaper (`url_shortener::jsonString`), and never tokenizes JSON itself
 * (contract C9). The screen (06.0/12 UI layer) owns the live @ref StatusFilter
 * and filter-bar text and calls these to turn each page query into one
 * `link.list` envelope.
 */

/**
 * @brief The D3 "status" radio choice, in D3 radio display order.
 *
 * Maps one-to-one to a `link.list` `status` wire value (@ref status_to_wire).
 * The TUI always sends `status` explicitly, including @ref StatusFilter::all
 * (decision log Q9), rather than relying on the descriptor's `active` default.
 * The `link.list` command additionally accepts `"expired"`, but the D3 radio
 * exposes only these four choices (the ADR 0003 D3 sketch); an expired-only
 * view is reached by the status column, not a radio position.
 */
enum class StatusFilter
{
    ///< `"all"`: every link regardless of status (the D3 default).
    all,

    ///< `"active"`: enabled, non-expired, non-deleted links.
    active,

    ///< `"disabled"`: links whose enabled flag is off.
    disabled,

    ///< `"deleted"`: soft-deleted links.
    deleted
};

/**
 * @brief Returns the `link.list` `status` wire string for a @ref StatusFilter.
 *
 * @param status The radio choice to translate.
 * @return One of `"all"`, `"active"`, `"disabled"`, `"deleted"`; `"all"` for
 * any unrecognized value (defensive, never produced by well-formed code).
 */
std::string status_to_wire(StatusFilter status);

/**
 * @brief The parsed Links filter: the free-text query plus the `tag:` / `camp:`
 * scoped terms and the status radio choice.
 *
 * A plain value type produced by @ref parse_filter_text from the filter-bar
 * text and the current radio choice. Empty string members are omitted from the
 * built request (@ref build_link_list_request); @ref status is always sent.
 */
struct LinkListFilter
{
    ///< Free-text search (`q`); the filter-bar words that carried no recognized
    ///< `tag:` / `camp:` prefix, space-joined. Empty when none.
    std::string q;

    ///< The `tag:` scoped term's value; empty when the filter had no `tag:`
    ///< prefix.
    std::string tag;

    ///< The `camp:` scoped term's value (the campaign name); empty when the
    ///< filter had no `camp:` prefix.
    std::string campaign;

    ///< The status radio choice; always sent on the request.
    StatusFilter status = StatusFilter::all;
};

/**
 * @brief Parses the D3 filter-bar text into a @ref LinkListFilter.
 *
 * Splits @p text on ASCII whitespace and classifies each token: a token
 * beginning `camp:` sets @ref LinkListFilter::campaign to the text after the
 * prefix (which may be empty, the `camp:` case); a token beginning `tag:` sets
 * @ref LinkListFilter::tag the same way; every other token - including one with
 * an unrecognized `x:` prefix - is appended to the free-text @ref
 * LinkListFilter::q, space-joined in order. Quote characters are not special:
 * they are kept verbatim as part of whichever value the token falls into (the
 * registry validates the final value). The last `camp:` / `tag:` token wins if
 * more than one is present.
 *
 * @param text The raw filter-bar text (`q` buffer from the table view).
 * @param status The current status radio choice, copied into the result.
 * @return The parsed filter; its string members are the trimmed/space-joined
 * values and @ref LinkListFilter::status is @p status.
 */
LinkListFilter parse_filter_text(const std::string& text, StatusFilter status);

/**
 * @brief Builds the exact `link.list` request for a filter and page window.
 *
 * Emits the `args` object with members in catalog order
 * (`status`,`q`,`tag`,`campaign`,`sort`,`order`,`offset`,`limit`), always
 * including `status` (decision log Q9), `offset` and `limit`, and including
 * `q`/`tag`/`campaign` only when non-empty. `sort` is emitted only when
 * @ref PageQuery::sort_key is one of the allowed columns `created_at`, `slug`
 * or `updated_at` (any other value, including empty, is dropped so the server
 * never sees an unsupported sort); when `sort` is emitted, `order` follows it
 * as
 * `"desc"` / `"asc"` from @ref PageQuery::descending. `limit` is the page size
 * (@ref PageQuery::limit) and `offset` the page offset (@ref
 * PageQuery::offset). Every string value is escaped with
 * `url_shortener::jsonString`, so a hostile filter cannot break the envelope
 * (contract C9).
 *
 * @param filter The parsed filter (status always sent; empty terms omitted).
 * @param page The current page window (offset, limit, sort, order).
 * @param request_id The correlation id to place in @ref
 * commands::CommandRequest::id, echoed back in the response.
 * @return A `v == 1` request for `link.list` with the composed `args`; its
 * `options` are left at their defaults (no dry-run).
 */
commands::CommandRequest build_link_list_request(const LinkListFilter& filter,
                                                 const PageQuery& page,
                                                 const std::string& request_id);

/**
 * @brief Builds a single-slug `link.*` request, e.g. for `link.enable`,
 * `link.disable`, `link.delete`, `link.restore` or `link.preview`.
 *
 * Keeps JSON construction in the model library (not the screen): the `args`
 * object is exactly `{"slug":<escaped>}` with @p slug escaped by
 * `url_shortener::jsonString`, so a hostile slug cannot break the envelope
 * (contract C9). The confirm decision is still made by @ref ConfirmPolicy from
 * the built args, not from the command name (threat model T-116, review 2).
 *
 * @param command The `link.*` command name to invoke.
 * @param slug The target link slug; escaped into the `args` object.
 * @param request_id The correlation id to echo back.
 * @return A `v == 1` request for @p command with `{"slug":...}` args and
 * default `options`.
 */
commands::CommandRequest build_slug_request(const std::string& command,
                                            const std::string& slug,
                                            const std::string& request_id);

}  // namespace url_shortener::tui
