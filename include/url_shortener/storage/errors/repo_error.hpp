#pragma once

#include <ostream>

/**
 * @brief Error categories for metadata repository operations, returned
 * via the `bool` + `*error` out-param convention on
 * `IMetadataRepository` methods.
 */
enum class RepoError
{
    ///< No error; the operation completed successfully.
    none,

    ///< The requested short code or record does not exist in the
    ///< repository.
    not_found,

    ///< A record with the same short code already exists; the create
    ///< was rejected without modifying any state.
    already_exists,

    ///< A caller-supplied value failed a precondition (e.g. an
    ///< empty `target_url` or a `short_code` that does not match
    ///< the required format); the operation did not run.
    invalid_argument,

    ///< A temporary backend failure occurred (e.g. a lost database
    ///< connection); the operation may succeed on retry.
    transient_failure,

    ///< An unrecoverable backend error occurred; the operation
    ///< cannot succeed without operator intervention.
    permanent_failure,
};

/**
 * @brief Stream formatter for repository error codes.
 * @param os Stream to write to.
 * @param e Value to format.
 * @return `os`, with the enumerator's symbolic name written to it
 * (e.g. `"RepoError::none"`).
 */
inline std::ostream& operator<<(std::ostream& os, RepoError e)
{
    switch (e) {
        case RepoError::none:
            return os << "RepoError::none";
        case RepoError::not_found:
            return os << "RepoError::not_found";
        case RepoError::already_exists:
            return os << "RepoError::already_exists";
        case RepoError::invalid_argument:
            return os << "RepoError::invalid_argument";
        case RepoError::transient_failure:
            return os << "RepoError::transient_failure";
        case RepoError::permanent_failure:
            return os << "RepoError::permanent_failure";
    }
    return os << "RepoError(" << static_cast<int>(e) << ")";
}
