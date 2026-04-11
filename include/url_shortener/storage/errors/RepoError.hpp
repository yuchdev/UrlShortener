#pragma once

#include <ostream>

/**
 * @brief Metadata repository error categories.
 */
enum class RepoError {
    none,
    not_found,
    already_exists,
    invalid_argument,
    transient_failure,
    permanent_failure,
};

inline std::ostream& operator<<(std::ostream& os, RepoError e)
{
    switch (e) {
    case RepoError::none:              return os << "RepoError::none";
    case RepoError::not_found:         return os << "RepoError::not_found";
    case RepoError::already_exists:    return os << "RepoError::already_exists";
    case RepoError::invalid_argument:  return os << "RepoError::invalid_argument";
    case RepoError::transient_failure: return os << "RepoError::transient_failure";
    case RepoError::permanent_failure: return os << "RepoError::permanent_failure";
    }
    return os << "RepoError(" << static_cast<int>(e) << ")";
}
