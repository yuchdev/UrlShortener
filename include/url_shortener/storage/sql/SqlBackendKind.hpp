#pragma once

#include <ostream>

/**
 * @brief Supported SQL backend kinds for metadata persistence.
 */
enum class SqlBackendKind {
    sqlite,
    postgres,
};

inline std::ostream& operator<<(std::ostream& os, SqlBackendKind kind)
{
    switch (kind) {
        case SqlBackendKind::sqlite:   return os << "sqlite";
        case SqlBackendKind::postgres: return os << "postgres";
        default:                       return os << "unknown";
    }
}
