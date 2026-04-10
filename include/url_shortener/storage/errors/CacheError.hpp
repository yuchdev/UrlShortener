#pragma once

/**
 * @brief Cache store error categories.
 */
enum class CacheError {
    none,
    unavailable,
    invalid_argument,
    permanent_failure,
};
