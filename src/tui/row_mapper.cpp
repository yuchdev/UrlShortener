#include <url_shortener/tui/row_mapper.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

///< Placeholder shown for a missing, null or non-scalar cell value (D3).
constexpr char k_empty_cell[] = "-";

///< Hard cap on the number of elements mapped into rows, independent of how
///< many the server returns. Above the catalog's largest page (`db.rows` limit
///< 500) it bounds per-call work even if a backend over-returns (threat model
///< T-121); `total` still reflects the payload's reported count.
constexpr std::size_t k_max_rows = 1000;

/**
 * @brief Resolves one (possibly dotted) key against an element's JSON object to
 * a display string.
 *
 * Nested object steps (`stats.total_redirects`) are followed with
 * @ref raw_field; the final key is read as a string, then an integer, then a
 * boolean, matching the leaf's JSON type.
 *
 * @param element_json The element's exact JSON object text.
 * @param key The field key or dotted path to resolve.
 * @return The decoded string / stringified number / `"true"`/`"false"`, or
 * @ref k_empty_cell when any step is missing or the leaf is null or a container.
 */
std::string cell_value(const std::string& element_json, const std::string& key)
{
    std::string current = element_json;
    std::size_t start = 0;
    // Walk every dotted segment except the last through nested objects.
    while (true)
    {
        const std::size_t dot = key.find('.', start);
        if (dot == std::string::npos)
        {
            break;
        }
        const std::string segment = key.substr(start, dot - start);
        std::optional<std::string> nested = raw_field(current, segment);
        if (!nested.has_value())
        {
            return k_empty_cell;
        }
        current = std::move(*nested);
        start = dot + 1;
    }

    const std::string leaf = key.substr(start);
    if (std::optional<std::string> s = get_string(current, leaf))
    {
        return *s;
    }
    if (std::optional<std::int64_t> n = get_int(current, leaf))
    {
        return std::to_string(*n);
    }
    if (std::optional<bool> b = get_bool(current, leaf))
    {
        return *b ? "true" : "false";
    }
    return k_empty_cell;
}

}  // namespace

std::optional<PageResult> rows_from_result(
    const std::string& result_json,
    const std::vector<std::string>& column_keys,
    const std::string& items_field)
{
    std::optional<std::string> array = raw_field(result_json, items_field);
    if (!array.has_value())
    {
        return std::nullopt;  // missing array field / malformed payload
    }

    PageResult page;
    const std::vector<std::string> elements = split_array(*array);
    const std::size_t kept = std::min(elements.size(), k_max_rows);
    page.raw_rows.reserve(kept);
    page.rows.reserve(kept);

    for (std::size_t r = 0; r < kept; ++r)
    {
        const std::string& element = elements[r];
        std::vector<std::string> cells;
        cells.reserve(column_keys.size());
        for (const std::string& key : column_keys)
        {
            cells.push_back(cell_value(element, key));
        }
        page.rows.push_back(std::move(cells));
        page.raw_rows.push_back(element);
    }

    if (std::optional<std::int64_t> total = get_int(result_json, "total"))
    {
        page.total = *total < 0 ? 0 : static_cast<std::size_t>(*total);
    }
    else
    {
        page.total = page.rows.size();
    }
    return page;
}

}  // namespace url_shortener::tui
