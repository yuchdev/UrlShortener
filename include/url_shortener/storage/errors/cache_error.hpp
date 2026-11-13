#pragma once

#include <ostream>

/**
 * @brief Error categories for cache store operations, returned via
 * the `bool` + `*error` out-param convention on `ICacheStore`
 * methods (`Get`, `Set`, `Delete`, `ClearByPrefix`).
 */
enum class CacheError
{
    ///< No error; the operation completed successfully (or, for
    ///< `Get`, the key was simply absent — a miss is not an error).
    none,

    ///< The cache backend is temporarily unavailable; the caller
    ///< should fall through to the authoritative store (fail-open).
    unavailable,

    ///< A caller-supplied key or value failed a precondition (e.g.
    ///< an empty key); the operation did not run.
    invalid_argument,

    ///< An unrecoverable backend error occurred; the cache entry
    ///< was not read or written.
    permanent_failure,
};

/**
 * @brief Stream formatter for cache error codes.
 * @param os Stream to write to.
 * @param e Value to format.
 * @return `os`, with the enumerator's symbolic name written to it
 * (e.g. `"CacheError::none"`).
 */
inline std::ostream& operator<<(std::ostream& os, CacheError e)
{
    switch (e) {
        case CacheError::none:
            return os << "CacheError::none";
        case CacheError::unavailable:
            return os << "CacheError::unavailable";
        case CacheError::invalid_argument:
            return os << "CacheError::invalid_argument";
        case CacheError::permanent_failure:
            return os << "CacheError::permanent_failure";
    }
    return os << "CacheError(" << static_cast<int>(e) << ")";
}
