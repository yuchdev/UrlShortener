#pragma once

#include <optional>
#include <string>

namespace url_shortener
{
namespace app
{

/**
 * @brief Error categories for application-layer command and query
 * operations performed by `LinkCommandService`.
 *
 * Returned through `Result<T>::error.code`; callers should check
 * `Result::ok()` rather than comparing this value directly against
 * `none`.
 */
enum class AppErrorCode
{
    ///< No error; the operation completed as requested.
    none,

    ///< The supplied target URL failed validation: not an absolute
    ///< HTTP/HTTPS URL, or its host resolves to a private/loopback
    ///< address when `SHORTENER_ALLOW_PRIVATE_TARGETS` is unset.
    invalid_url,

    ///< The supplied slug failed format validation (e.g. contains
    ///< characters outside the allowed set or exceeds the maximum
    ///< allowed length).
    invalid_slug,

    ///< The supplied slug is a system-reserved value (e.g. `api`,
    ///< `r`, `healthz`) and cannot be used for a user-created link.
    reserved_slug,

    ///< The slug is already in use by an existing link; the create
    ///< call did not persist anything.
    slug_conflict,

    ///< A field value failed validation: `expires_at` was not
    ///< RFC3339 UTC, `tags` violated count or length constraints,
    ///< `metadata` keys or values exceeded limits, or `campaign`
    ///< fields exceeded allowed lengths.
    invalid_field,

    ///< The requested link (by slug or stable ID) was not found
    ///< in storage.
    not_found,

    ///< The underlying storage backend rejected or failed the
    ///< operation; no mutation was persisted.
    storage_failure,

    ///< An unexpected internal error not attributable to
    ///< caller-supplied input (e.g. the slug generator exhausted
    ///< all unique-candidate attempts).
    internal
};

/**
 * @brief Application-layer error used in `Result<T>::error` and the
 * `bool` + `AppError*` out-param convention.
 *
 * Callers should check `Result::ok()` or the method's `bool` return
 * rather than testing `code == AppErrorCode::none` directly.
 */
struct AppError
{
    ///< Failure category; `none` on a default-constructed or
    ///< untouched instance.
    AppErrorCode code = AppErrorCode::none;

    ///< Human-readable detail for logs and diagnostics; not
    ///< guaranteed stable across versions and not intended for
    ///< verbatim display to end users.
    std::string detail;
};

/**
 * @brief Tagged-union result returned by `LinkCommandService`
 * methods: either a successful value of type `T` or an `AppError`.
 *
 * @tparam T Value type held on success; must be movable.
 */
template<typename T>
struct Result
{
    ///< Holds the result value when the operation succeeded
    ///< (`error.code == AppErrorCode::none`); empty on any failure.
    std::optional<T> value;

    ///< Error detail; `error.code == AppErrorCode::none` indicates
    ///< success (equivalent to calling `ok()`).
    AppError error;

    /**
     * @brief Indicates whether the operation succeeded.
     * @return `true` if `error.code == AppErrorCode::none`; `false`
     * on any failure code.
     */
    bool ok() const { return error.code == AppErrorCode::none; }
};

}  // namespace app
}  // namespace url_shortener
