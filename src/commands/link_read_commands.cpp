#include <url_shortener/commands/link_commands.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_command_codec.hpp>

namespace url_shortener::commands
{

namespace
{

/// Per-arg free-text byte cap (contract C3): a hard outer bound only, raised so
/// the registry never pre-empts the service's own validators (which keep their
/// REST messages). Mirrors `link.create` (subtask 07) and
/// `ServerConfig::max_request_body_bytes`.
constexpr std::size_t k_text_max_length = 65536;

/**
 * @brief Builds a free-text string @ref ArgSpec with the contract-C3 cap.
 *
 * @param name The argument key as it appears in the `args` object.
 * @param required `true` when the argument must be present (the registry then
 * fails a missing value with `"<name>: missing"`).
 * @return A string @ref ArgSpec named @p name with `max_length` raised to
 * @ref k_text_max_length so length validation stays in the service.
 */
ArgSpec make_string_arg(std::string name, bool required)
{
    ArgSpec spec;
    spec.name = std::move(name);
    spec.type = ArgType::string;
    spec.required = required;
    spec.max_length = k_text_max_length;
    return spec;
}

/**
 * @brief Builds a `slug`/`id` selector @ref ArgSpec in the `slug_or_id` group.
 *
 * Shared by `link.get` and `link.preview`: both arguments are optional strings
 * tagged with the same non-empty `one_of_group`, so the registry enforces that
 * exactly one is present (failing a zero-or-two case with `one_of_violation`)
 * before the handler runs.
 *
 * @param name The selector key (`slug` or `id`).
 * @param doc The one-line catalog description for the selector.
 * @return A string @ref ArgSpec named @p name in the `slug_or_id` group.
 */
ArgSpec make_selector_arg(std::string name, std::string doc)
{
    ArgSpec spec = make_string_arg(std::move(name), false);
    spec.one_of_group = "slug_or_id";
    spec.doc = std::move(doc);
    return spec;
}

/**
 * @brief The `slug`/`id` argument schema shared by `link.get`/`link.preview`.
 *
 * @return The two selector specs (`slug`, `id`) of the `slug_or_id` group.
 */
std::vector<ArgSpec> make_slug_or_id_args()
{
    return {make_selector_arg("slug", "Look up by slug."),
            make_selector_arg(
                "id", "Look up by opaque id. Exactly one of slug/id.")};
}

/**
 * @brief Reads a string argument only when it is present and non-empty.
 *
 * Reproduces the REST stats shape selection, which treats an absent query
 * param and an empty one identically (`!param.empty()`): an explicit empty
 * string is therefore treated as "not supplied" for range-mode detection.
 *
 * @param args The validated argument view.
 * @param name The argument key to read.
 * @return The decoded value when present and non-empty; `std::nullopt` when
 * absent, JSON null, or an empty string.
 */
std::optional<std::string> non_empty_string(const ArgsView& args,
                                             std::string_view name)
{
    std::optional<std::string> value = args.get_string(name);
    if (value && value->empty()) {
        return std::nullopt;
    }
    return value;
}

/**
 * @brief Handler for `link.get`: resolve the addressed link and serialize it.
 *
 * Selects the single present `slug`/`id` via @ref decode_slug_or_id, delegates
 * to `app::LinkCommandService::GetLink` (the by-`id` path uses the indexed
 * `findById` lookup, never a scan), and projects the outcome with
 * @ref link_view_result. No field or identifier validation is performed here;
 * the `slug_or_id` one-of group has already been enforced by the registry.
 *
 * @param service The service the lookup is delegated to.
 * @param args The validated argument view.
 * @return On success a @ref CommandResult carrying `serializeLinkViewJson` and
 * the view's slug as audit target; on `not_found` the translated error with the
 * addressed slug/id as audit target; on the defensive zero-selector path an
 * `invalid_field` `one_of_violation`.
 */
CommandResult handle_link_get(app::LinkCommandService& service,
                              const ArgsView& args)
{
    app::GetLinkQuery query;
    CommandError error;
    if (!decode_slug_or_id(args, &query, &error)) {
        CommandResult result;
        result.error = std::move(error);
        return result;
    }

    const app::Result<app::LinkView> result = service.GetLink(query);
    return link_view_result(result, query.value);
}

/**
 * @brief Handler for `link.preview`: resolve the addressed link's preview.
 *
 * Identical control flow to @ref handle_link_get but delegates to
 * `app::LinkCommandService::PreviewLink` and projects the reduced preview body
 * through @ref link_preview_result.
 *
 * @param service The service the lookup is delegated to.
 * @param args The validated argument view.
 * @return On success a @ref CommandResult carrying `serializeLinkPreviewJson`
 * and the view's slug as audit target; on `not_found` the translated error with
 * the addressed slug/id as audit target; on the defensive zero-selector path an
 * `invalid_field` `one_of_violation`.
 */
CommandResult handle_link_preview(app::LinkCommandService& service,
                                  const ArgsView& args)
{
    app::GetLinkQuery query;
    CommandError error;
    if (!decode_slug_or_id(args, &query, &error)) {
        CommandResult result;
        result.error = std::move(error);
        return result;
    }

    const app::Result<app::LinkView> result = service.PreviewLink(query);
    return link_preview_result(result, query.value);
}

/**
 * @brief Projects a lifetime-mode `GetLink` outcome as a @ref CommandResult.
 *
 * Builds the six-key lifetime-counter body from `app::LinkView` via
 * `app::serializeLinkLifetimeStatsJson` (called, never re-implemented: contract
 * C1/C3). The audit target is the caller-addressed @p slug on every outcome.
 *
 * @param result The `GetLink` outcome for the addressed slug.
 * @param slug The caller-addressed slug, recorded as the audit target.
 * @return On success a @ref CommandResult with the lifetime-stats body; on a
 * service failure the translated error; on an `ok()` result with no value an
 * `internal` error.
 */
CommandResult lifetime_stats_result(const app::Result<app::LinkView>& result,
                                    std::string_view slug)
{
    CommandResult out;
    if (result.ok() && result.value) {
        out.result_json = app::serializeLinkLifetimeStatsJson(*result.value);
        out.audit_target = std::string(slug);
        return out;
    }
    if (result.ok()) {
        out.error =
            make_error(CommandErrorCode::internal, "service returned no value");
    }
    else {
        out.error = to_command_error(result.error);
    }
    out.audit_target = std::string(slug);
    return out;
}

/**
 * @brief Handler for `link.stats`: pick the range or lifetime result shape.
 *
 * Reproduces `handleLinkStats` exactly. When `from`, `to` and `bucket` are all
 * present and non-empty, delegates to `app::LinkCommandService::GetLinkStats`
 * and projects the range report via @ref link_stats_result; the reader is the
 * sole validator of `from`/`to`/`bucket`, so a bad timestamp or bucket surfaces
 * as its `invalid_timestamp` / `invalid_bucket` detail (contract C3). Otherwise
 * (any of the three absent) it delegates to `GetLink` for the addressed slug and
 * emits the lifetime-counter body; the extra range args, if partially supplied,
 * are ignored exactly as REST ignores them today. The handler never parses the
 * timestamps or bucket itself.
 *
 * @param service The service the lookup is delegated to.
 * @param args The validated argument view (`slug` is required by the schema).
 * @return A @ref CommandResult carrying either the range report or the
 * lifetime-counter body, or the translated service error.
 */
CommandResult handle_link_stats(app::LinkCommandService& service,
                                const ArgsView& args)
{
    // slug is required; the registry's ArgSpec presence check has already
    // guaranteed it is present as a string.
    const std::string slug = args.get_string("slug").value_or("");

    const std::optional<std::string> from = non_empty_string(args, "from");
    const std::optional<std::string> to = non_empty_string(args, "to");
    const std::optional<std::string> bucket = non_empty_string(args, "bucket");

    if (from && to && bucket) {
        app::GetLinkStatsQuery query;
        query.slug = slug;
        query.from = *from;
        query.to = *to;
        query.bucket = *bucket;
        const app::Result<app::LinkStatsView> result =
            service.GetLinkStats(query);
        return link_stats_result(result, slug);
    }

    app::GetLinkQuery query;
    query.by = app::GetLinkBy::slug;
    query.value = slug;
    const app::Result<app::LinkView> result = service.GetLink(query);
    return lifetime_stats_result(result, slug);
}

}  // namespace

CommandDescriptor make_link_get_descriptor(app::LinkCommandService& service)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.get";
    descriptor.summary = "Retrieve a short link by slug or id.";
    descriptor.args = make_slug_or_id_args();
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/links/{slug}",
                          "GET /api/v1/links/id/{id}",
                          "link get"};
    descriptor.handler =
        [&service](CommandContext& /*context*/, const ArgsView& args)
    {
        return handle_link_get(service, args);
    };
    return descriptor;
}

CommandDescriptor make_link_preview_descriptor(app::LinkCommandService& service)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.preview";
    descriptor.summary = "Preview a short link's resolved status.";
    descriptor.args = make_slug_or_id_args();
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/links/{slug}/preview", "link preview"};
    descriptor.handler =
        [&service](CommandContext& /*context*/, const ArgsView& args)
    {
        return handle_link_preview(service, args);
    };
    return descriptor;
}

CommandDescriptor make_link_stats_descriptor(app::LinkCommandService& service)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.stats";
    descriptor.summary = "Retrieve redirect statistics for a short link.";

    ArgSpec from = make_string_arg("from", false);
    from.doc = "Range start, decimal Unix epoch seconds.";

    ArgSpec to = make_string_arg("to", false);
    to.doc = "Range end, decimal Unix epoch seconds.";

    ArgSpec bucket = make_string_arg("bucket", false);
    bucket.doc = "Time-bucket size: hour|day|week.";

    descriptor.args = {make_string_arg("slug", true),
                       std::move(from),
                       std::move(to),
                       std::move(bucket)};
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/links/{slug}/stats", "link stats"};
    descriptor.handler =
        [&service](CommandContext& /*context*/, const ArgsView& args)
    {
        return handle_link_stats(service, args);
    };
    return descriptor;
}

}  // namespace url_shortener::commands
