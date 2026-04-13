#pragma once

#include <exception>

#include "url_shortener/storage/errors/RepoError.hpp"

/**
 * @brief Maps PostgreSQL/SOCI failures into repository error taxonomy.
 */
class PostgresErrorMapper {
public:
    RepoError MapException(const std::exception& ex) const;
};
