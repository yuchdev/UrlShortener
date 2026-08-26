#pragma once

#include <exception>

#include "url_shortener/storage/errors/repo_error.hpp"

/**
 * @brief Maps SQLite/SOCI exceptions to repository error taxonomy.
 */
class SqliteErrorMapper {
public:
    RepoError MapException(const std::exception& ex) const;
};
