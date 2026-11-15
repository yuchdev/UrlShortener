#include <url_shortener/commands/link_command_codec.hpp>

#include <string>
#include <utility>

#include <url_shortener/app/link_command_service.hpp>

namespace url_shortener::commands
{

CommandError to_command_error(const app::AppError& error)
{
    return make_error(from_app_error(error.code), error.detail);
}

namespace
{

/**
 * @brief Shared body of the three link result wrappers.
 *
 * On a populated result the serializer output is embedded verbatim and the
 * view's own slug becomes the audit target; otherwise the service error is
 * mapped by @ref to_command_error (or `internal` for an `ok()` result with no
 * value) and the caller-addressed target is recorded.
 *
 * @tparam View The read-model view type (`LinkView` or `LinkStatsView`).
 * @tparam Serialize A callable mapping `const View&` to serialized JSON text.
 * @param result The service outcome to project.
 * @param audit_target The caller-addressed slug/id, used on any non-success.
 * @param serialize The shared REST serializer for @p View.
 * @return The projected @ref CommandResult.
 */
template<typename View, typename Serialize>
CommandResult make_view_result(const app::Result<View>& result,
                               std::string_view audit_target,
                               Serialize serialize)
{
    CommandResult out;
    if (result.ok() && result.value) {
        out.result_json = serialize(*result.value);
        out.audit_target = result.value->slug;
        return out;
    }
    if (result.ok()) {
        out.error =
            make_error(CommandErrorCode::internal, "service returned no value");
    }
    else {
        out.error = to_command_error(result.error);
    }
    out.audit_target = std::string(audit_target);
    return out;
}

}  // namespace

CommandResult link_view_result(const app::Result<app::LinkView>& result,
                               std::string_view audit_target)
{
    return make_view_result(result, audit_target, app::serializeLinkViewJson);
}

CommandResult link_preview_result(const app::Result<app::LinkView>& result,
                                  std::string_view audit_target)
{
    return make_view_result(
        result, audit_target, app::serializeLinkPreviewJson);
}

CommandResult link_stats_result(const app::Result<app::LinkStatsView>& result,
                                std::string_view audit_target)
{
    return make_view_result(result, audit_target, app::serializeLinkStatsJson);
}

bool decode_campaign(const ArgsView& args,
                     std::string_view name,
                     std::optional<Link::Campaign>* out,
                     CommandError* error)
{
    const std::optional<ArgsView> object = args.get_object(name);
    if (!object) {
        if (error) {
            *error = make_error(CommandErrorCode::internal,
                                "campaign: internal decode error");
        }
        return false;
    }

    Link::Campaign campaign;
    campaign.name = object->get_string("name");
    campaign.source = object->get_string("source");
    campaign.medium = object->get_string("medium");
    campaign.term = object->get_string("term");
    campaign.content = object->get_string("content");
    campaign.id = object->get_string("id");

    if (out) {
        *out = std::move(campaign);
    }
    return true;
}

patch_state patch_state_of(const ArgsView& args, std::string_view name)
{
    if (!args.has(name)) {
        return patch_state::absent;
    }
    if (args.is_null(name)) {
        return patch_state::null_value;
    }
    return patch_state::value;
}

bool decode_slug_or_id(const ArgsView& args,
                       app::GetLinkQuery* out,
                       CommandError* error)
{
    std::optional<std::string> slug = args.get_string("slug");
    if (slug) {
        if (out) {
            out->by = app::GetLinkBy::slug;
            out->value = std::move(*slug);
        }
        return true;
    }

    std::optional<std::string> id = args.get_string("id");
    if (id) {
        if (out) {
            out->by = app::GetLinkBy::id;
            out->value = std::move(*id);
        }
        return true;
    }

    if (error) {
        *error = make_error(CommandErrorCode::invalid_field,
                            "slug: one_of_violation");
    }
    return false;
}

}  // namespace url_shortener::commands
