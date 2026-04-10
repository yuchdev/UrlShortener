#pragma once

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
