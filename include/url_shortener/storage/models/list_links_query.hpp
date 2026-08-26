#pragma once

#include <cstddef>
#include <optional>
#include <string>

/**
 * @brief Repository query options for list/read APIs.
 */
struct ListLinksQuery {
    std::optional<std::string> owner;
    bool include_inactive = true;
    bool include_deleted = false;
    std::size_t offset = 0;
    std::size_t limit = 100;
};
