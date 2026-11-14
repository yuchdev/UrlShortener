#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/analytics/click_event.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Upper bound every `IClickEventRepository::ListEvents` implementation
 * clamps `ClickEventQuery::limit` to, so a single management-plane read can
 * never materialize an unbounded window regardless of the requested `limit`.
 * This is a listing cap only; it does not restrict how many events a backend
 * stores or counts into `ClickEventPage::total`.
 */
constexpr std::size_t kMaxClickEventPageSize = 500;

/**
 * @brief Field a `ClickEventQuery` is ordered by before paging. The sort is
 * always stable, with `ClickEvent::event_id` ascending as the final tie-break
 * (see `IClickEventRepository::ListEvents`).
 */
enum class ClickEventSort
{
    ///< Order by `ClickEvent::occurred_at` (the default).
    occurred_at,

    ///< Order by `ClickEvent::slug` lexicographically.
    slug,

    ///< Order by `ClickEvent::status_code` numerically.
    status_code
};

/**
 * @brief Filter, ordering, and paging parameters for a bounded read of
 * individual click events via `IClickEventRepository::ListEvents`.
 *
 * All filter fields are optional and combined with logical AND; an unset
 * field imposes no constraint. `from`/`to` describe a half-open time range
 * (`from <= occurred_at < to`). This is a management-plane query and must
 * never be issued from the redirect hot path.
 */
struct ClickEventQuery
{
    ///< Exact-match slug filter; unset means no slug filter.
    std::optional<std::string> slug;

    ///< Case-insensitive substring matched against `ClickEvent::slug` OR
    ///< `ClickEvent::domain`; an event matches if either contains it. Unset
    ///< means no text filter.
    std::optional<std::string> text;

    ///< Exact-match HTTP status-code filter; unset means no status filter.
    std::optional<std::uint16_t> status_code;

    ///< Inclusive lower bound on `ClickEvent::occurred_at`; unset means
    ///< unbounded below.
    std::optional<Timestamp> from;

    ///< Exclusive upper bound on `ClickEvent::occurred_at`; unset means
    ///< unbounded above.
    std::optional<Timestamp> to;

    ///< Field the matched events are ordered by before paging.
    ClickEventSort sort_by = ClickEventSort::occurred_at;

    ///< `true` sorts `sort_by` descending (newest/largest first); `false`
    ///< ascending. The `event_id` tie-break is always ascending regardless.
    bool descending = true;

    ///< Number of leading matched events to skip before collecting `items`.
    std::size_t offset = 0;

    ///< Maximum number of events to return in `items`; clamped to
    ///< `kMaxClickEventPageSize` by every implementation. `0` yields an empty
    ///< `items` while still reporting the full `total`.
    std::size_t limit = 50;
};

/**
 * @brief One page of click events returned by
 * `IClickEventRepository::ListEvents`.
 */
struct ClickEventPage
{
    ///< The requested window: matched events after ordering, `offset`, and
    ///< the clamped `limit` are applied. Never larger than
    ///< `kMaxClickEventPageSize`.
    std::vector<ClickEvent> items;

    ///< Total number of events matching the filters, computed before `offset`
    ///< and `limit` are applied, so callers can page without re-counting.
    std::uint64_t total = 0;
};

}  // namespace url_shortener::analytics
