#include <cctype>
#include <string>

#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/links_query.hpp>

namespace url_shortener::tui
{

namespace
{

/// The `camp:` filter-term prefix.
constexpr char k_campaign_prefix[] = "camp:";

/// The `tag:` filter-term prefix.
constexpr char k_tag_prefix[] = "tag:";

/// The columns the TUI may ask `link.list` to sort by (catalog 3.2). Any other
/// sort key is dropped so the server never sees an unsupported value.
bool is_allowed_sort(const std::string& sort_key)
{
    return sort_key == "created_at" || sort_key == "slug"
        || sort_key == "updated_at";
}

/// Whether @p text starts with @p prefix.
bool starts_with(const std::string& text, const char* prefix)
{
    const std::string p(prefix);
    return text.size() >= p.size() && text.compare(0, p.size(), p) == 0;
}

}  // namespace

std::string status_to_wire(StatusFilter status)
{
    switch (status) {
        case StatusFilter::all:
            return "all";
        case StatusFilter::active:
            return "active";
        case StatusFilter::disabled:
            return "disabled";
        case StatusFilter::deleted:
            return "deleted";
    }
    return "all";
}

LinkListFilter parse_filter_text(const std::string& text, StatusFilter status)
{
    LinkListFilter filter;
    filter.status = status;

    std::string q;
    std::size_t i = 0;
    while (i < text.size()) {
        // Skip leading whitespace.
        while (i < text.size()
               && std::isspace(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        // Collect one whitespace-delimited token.
        const std::size_t start = i;
        while (i < text.size()
               && !std::isspace(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        if (i == start) {
            break;
        }
        const std::string token = text.substr(start, i - start);

        if (starts_with(token, k_campaign_prefix)) {
            filter.campaign =
                token.substr(std::string(k_campaign_prefix).size());
        }
        else if (starts_with(token, k_tag_prefix)) {
            filter.tag = token.substr(std::string(k_tag_prefix).size());
        }
        else {
            if (!q.empty()) {
                q += ' ';
            }
            q += token;
        }
    }
    filter.q = std::move(q);
    return filter;
}

commands::CommandRequest build_link_list_request(const LinkListFilter& filter,
                                                 const PageQuery& page,
                                                 const std::string& request_id)
{
    // Emit the args in catalog order; status/offset/limit are always present,
    // the scoped terms only when set, sort/order only for an allowed column.
    std::string args = "{";
    args += "\"status\":"
        + url_shortener::jsonString(status_to_wire(filter.status));
    if (!filter.q.empty()) {
        args += ",\"q\":" + url_shortener::jsonString(filter.q);
    }
    if (!filter.tag.empty()) {
        args += ",\"tag\":" + url_shortener::jsonString(filter.tag);
    }
    if (!filter.campaign.empty()) {
        args += ",\"campaign\":" + url_shortener::jsonString(filter.campaign);
    }
    if (is_allowed_sort(page.sort_key)) {
        args += ",\"sort\":" + url_shortener::jsonString(page.sort_key);
        args += ",\"order\":"
            + url_shortener::jsonString(page.descending ? "desc" : "asc");
    }
    args += ",\"offset\":" + std::to_string(page.offset);
    args += ",\"limit\":" + std::to_string(page.limit);
    args += "}";

    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = "link.list";
    request.args_json = std::move(args);
    return request;
}

commands::CommandRequest build_slug_request(const std::string& command,
                                            const std::string& slug,
                                            const std::string& request_id)
{
    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = command;
    request.args_json = "{\"slug\":" + url_shortener::jsonString(slug) + "}";
    return request;
}

}  // namespace url_shortener::tui
