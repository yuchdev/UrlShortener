/**
 * @file link_lister.cpp
 * @brief Query validation and page serialization for the application-level
 * `link.list` browse port (subtask 12). The repository-backed implementation
 * of `ILinkLister` lives in `repository_link_lister.cpp`.
 */
#include <sstream>
#include <string>

#include <url_shortener/app/link_lister.hpp>

namespace url_shortener
{
namespace app
{

AppError validate_link_list_query(const LinkListQuery& query)
{
    // `limit` is checked before `q` so the field named in `detail` is
    // deterministic when both are out of bounds. Both branches name the field
    // only, never the offending value (threat model T-56 / fixed-string rule).
    if (query.limit < 1 || query.limit > link_list_max_limit) {
        return AppError {AppErrorCode::invalid_field, "limit"};
    }
    if (query.q.has_value() && query.q->size() > link_list_max_query_length) {
        return AppError {AppErrorCode::invalid_field, "q"};
    }
    return AppError {};
}

std::string serializeLinkListJson(const LinkListPage& page)
{
    std::ostringstream os;
    os << "{\"items\":[";
    for (std::size_t i = 0; i < page.items.size(); ++i) {
        if (i != 0) {
            os << ',';
        }
        // Item shape is the shared `link.get` body verbatim (contract C1/C3):
        // no list-specific item projection exists.
        os << serializeLinkViewJson(page.items[i]);
    }
    os << "],\"offset\":" << page.offset << ",\"limit\":" << page.limit
       << ",\"total\":" << page.total << '}';
    return os.str();
}

}  // namespace app
}  // namespace url_shortener
