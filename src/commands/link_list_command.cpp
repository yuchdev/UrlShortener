#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/browse_commands.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_command_codec.hpp>

namespace url_shortener::commands
{

namespace
{

/// Byte cap on the `tag` filter, matching the service's per-tag length limit
/// (32 bytes per tag). A longer filter value could never match a stored
/// tag, so this bound only pre-empts pointless scan work (threat model T-56)
/// and never rejects a value that could succeed.
constexpr std::size_t k_tag_max_length = 32;

/// Byte cap on the `campaign` filter, matching the service's campaign-field
/// length limit (128 bytes). A longer value could
/// never equal any stored `campaign.name`, so this bound only pre-empts
/// pointless scan work (threat model T-56).
constexpr std::size_t k_campaign_max_length = 128;

/**
 * @brief Builds a free-text string @ref ArgSpec with an explicit byte cap.
 *
 * @param name The argument key as it appears in the `args` object.
 * @param max_length Maximum decoded byte length; a longer value fails the
 * registry's validation with reason `too_long` before the handler runs.
 * @return An optional string @ref ArgSpec named @p name capped at @p
 * max_length.
 */
ArgSpec make_string_arg(std::string name, std::size_t max_length)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    spec.required = false;
    spec.max_length = max_length;
    return spec;
}

/**
 * @brief Builds an enumeration @ref ArgSpec with its allowed values and a
 * documented default.
 *
 * @param name The argument key as it appears in the `args` object.
 * @param values The case-sensitively compared set of accepted string values; a
 * value outside the set fails with reason `not_in_enum` before the handler
 * runs.
 * @param default_token The default enum value applied by the handler when the
 * argument is omitted; recorded (as a JSON string literal) for help/catalog
 * only
 * (@ref validate_args never reads it). Must be one of @p values.
 * @return An optional enumeration @ref ArgSpec named @p name.
 */
ArgSpec make_enum_arg(std::string name,
                      std::vector<std::string> values,
                      const std::string& default_token)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::enumeration;
    spec.enum_values = std::move(values);
    // The descriptor records each default as a verbatim JSON token
    // (command.list embeds it), so an enum string default must be quoted.
    spec.default_value = "\"" + default_token + "\"";
    return spec;
}

/**
 * @brief Builds the argument schema for `link.list` (catalog 3.2 table).
 *
 * Declares every bounded-work constraint on the descriptor so an out-of-bounds
 * query is rejected by the registry before the lister scans (threat model
 * T-56): `limit` is an integer in `1..link_list_max_limit`, `offset` an integer
 * in `0..link_list_max_offset`, `q` a string capped at
 * `link_list_max_query_length`, and `tag`/
 * `campaign` strings capped at the service's tag/campaign length limits. The
 * `status`/`sort`/`order` enumerations restrict their values so a bad token is
 * `not_in_enum` rather than reaching the handler. The subtask-12 bounds
 * constants are used verbatim; the lister re-runs `validate_link_list_query` as
 * defense in depth.
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_link_list_args()
{
    ArgSpec offset;
    offset.name = "offset";
    offset.type = ArgType::integer;
    offset.min = 0;
    // Cap the offset the same way as the limit above (threat model T-56): an
    // unbounded 18-digit offset would force a huge deep-skip index walk per
    // request. link_list_max_offset is documented next to link_list_max_limit.
    offset.max = static_cast<long long>(app::link_list_max_offset);
    offset.default_value = "0";
    offset.doc = "Zero-based index of the first link to return (0..1000000).";

    ArgSpec limit;
    limit.name = "limit";
    limit.type = ArgType::integer;
    limit.min = 1;
    limit.max = static_cast<long long>(app::link_list_max_limit);
    limit.default_value = "50";
    limit.doc = "Maximum number of links to return (1..500).";

    ArgSpec status =
        make_enum_arg("status",
                      {"all", "active", "disabled", "expired", "deleted"},
                      "active");
    status.doc = "Lifecycle filter; defaults to active.";

    ArgSpec q = make_string_arg("q", app::link_list_max_query_length);
    q.doc = "Case-insensitive substring filter on slug or target URL.";

    ArgSpec tag = make_string_arg("tag", k_tag_max_length);
    tag.doc = "Exact-match tag filter.";

    ArgSpec campaign = make_string_arg("campaign", k_campaign_max_length);
    campaign.doc = "Exact-match campaign name filter.";

    ArgSpec sort = make_enum_arg(
        "sort", {"created_at", "slug", "updated_at"}, "created_at");
    sort.doc = "Field the results are ordered by.";

    ArgSpec order = make_enum_arg("order", {"asc", "desc"}, "desc");
    order.doc = "Sort direction.";

    return {std::move(status),
            std::move(q),
            std::move(tag),
            std::move(campaign),
            std::move(sort),
            std::move(order),
            std::move(offset),
            std::move(limit)};
}

/**
 * @brief Maps a validated `status` token to its `app::list_status`.
 *
 * The enumeration @ref ArgSpec has already restricted the value to the five
 * accepted tokens, so this mapping is total on any value that reaches it.
 *
 * @param value One of `all`, `active`, `disabled`, `expired`, `deleted`.
 * @return The matching `app::list_status`; `active` for an unrecognized value
 * (unreachable given the enum spec) so the handler never under-filters.
 */
app::list_status status_of(const std::string& value)
{
    if (value == "all") {
        return app::list_status::all;
    }
    if (value == "disabled") {
        return app::list_status::disabled;
    }
    if (value == "expired") {
        return app::list_status::expired;
    }
    if (value == "deleted") {
        return app::list_status::deleted;
    }
    return app::list_status::active;
}

/**
 * @brief Maps a validated `sort` token to its `app::list_sort`.
 *
 * The enumeration @ref ArgSpec has already restricted the value to the three
 * accepted tokens, so this mapping is total on any value that reaches it.
 *
 * @param value One of `created_at`, `slug`, `updated_at`.
 * @return The matching `app::list_sort`; `created_at` for an unrecognized value
 * (unreachable given the enum spec).
 */
app::list_sort sort_of(const std::string& value)
{
    if (value == "slug") {
        return app::list_sort::slug;
    }
    if (value == "updated_at") {
        return app::list_sort::updated_at;
    }
    return app::list_sort::created_at;
}

/**
 * @brief Decodes the validated arguments into an `app::LinkListQuery`.
 *
 * Absent arguments keep the struct defaults (`active`, `created_at`,
 * descending, offset 0, limit 50). The `order` token maps `asc` to ascending
 * and `desc` to descending. Integer bounds (`offset 0..1000000`,
 * `limit 1..500`) and the enumeration/length caps were already enforced by the
 * registry, so the casts here cannot narrow a negative or over-large value.
 *
 * @param args The validated argument view.
 * @return The decoded browse query.
 */
app::LinkListQuery decode_query(const ArgsView& args)
{
    app::LinkListQuery query;

    if (const std::optional<std::string> status = args.get_string("status")) {
        query.status = status_of(*status);
    }

    query.q = args.get_string("q");
    query.tag = args.get_string("tag");
    query.campaign = args.get_string("campaign");

    if (const std::optional<std::string> sort = args.get_string("sort")) {
        query.sort = sort_of(*sort);
    }

    if (const std::optional<std::string> order = args.get_string("order")) {
        // The enum ArgSpec restricts the value to asc/desc; descending is the
        // default, so only an explicit "asc" flips it.
        query.descending = (*order == "desc");
    }

    if (const std::optional<long long> offset = args.get_int("offset")) {
        query.offset = static_cast<std::size_t>(*offset);
    }

    if (const std::optional<long long> limit = args.get_int("limit")) {
        query.limit = static_cast<std::size_t>(*limit);
    }

    return query;
}

/**
 * @brief Handler for `link.list`: decode the query, list, serialize the page.
 *
 * Builds the `app::LinkListQuery` via @ref decode_query, calls
 * `app::ILinkLister::list`, and on success returns the page serialized by
 * `app::serializeLinkListJson` (whose items are the shared
 * `serializeLinkViewJson` body: contract C1/C3). A lister failure is translated
 * by @ref to_command_error (fixed details only, contract C6); an `ok()` result
 * with no page is a lister programming error mapped to `internal`. The audit
 * target stays empty because this is a read command.
 *
 * @param lister The browse port the query is delegated to.
 * @param args The validated argument view (bounds already enforced by the
 * registry, so the lister is never reached for an out-of-bounds query).
 * @return On success a @ref CommandResult carrying the serialized page; on a
 * lister failure the translated @ref CommandError; on an `ok()` result with no
 * value an `internal` error.
 */
CommandResult handle_link_list(app::ILinkLister& lister, const ArgsView& args)
{
    const app::LinkListQuery query = decode_query(args);
    const app::Result<app::LinkListPage> result = lister.list(query);

    CommandResult out;
    if (result.ok() && result.value) {
        out.result_json = app::serializeLinkListJson(*result.value);
        return out;
    }
    if (result.ok()) {
        out.error =
            make_error(CommandErrorCode::internal, "service returned no value");
        return out;
    }
    out.error = to_command_error(result.error);
    return out;
}

}  // namespace

Permission link_status_permission(const ArgsView& args)
{
    if (const std::optional<std::string> status = args.get_string("status")) {
        if (*status == "all" || *status == "deleted") {
            return Permission::write;
        }
    }
    return Permission::read;
}

CommandDescriptor make_link_list_descriptor(app::ILinkLister& lister)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.list";
    descriptor.summary = "List short links with filtering, sorting and paging.";
    descriptor.args = make_link_list_args();
    descriptor.permission = Permission::read;
    // status=all|deleted raise the base read permission to write (T-71); the
    // single enforcement point shared by REST, POST /commands and cmd, and by
    // link.export (make_link_export_descriptor) via link_status_permission.
    descriptor.permission_for = link_status_permission;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/links"};
    descriptor.handler =
        [&lister](CommandContext& /*context*/, const ArgsView& args)
    { return handle_link_list(lister, args); };
    return descriptor;
}

}  // namespace url_shortener::commands
