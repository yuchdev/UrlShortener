#include "url_shortener/tui/system_info_view.hpp"

#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

std::optional<std::string>
backend_label_from_system_info(std::string_view result_json)
{
    const std::string json(result_json);
    // `backend` is an object; take its raw source, then read `metadata`.
    const std::optional<std::string> backend = raw_field(json, "backend");
    if (!backend)
    {
        return std::nullopt;
    }
    return get_string(*backend, "metadata");
}

}  // namespace url_shortener::tui
