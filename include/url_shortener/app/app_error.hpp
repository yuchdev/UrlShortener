#pragma once

#include <optional>
#include <string>

namespace url_shortener {
namespace app {

enum class AppErrorCode
{
    none,
    invalid_url,
    invalid_slug,
    reserved_slug,
    slug_conflict,
    invalid_field,
    not_found,
    storage_failure,
    internal
};

struct AppError
{
    AppErrorCode code = AppErrorCode::none;
    std::string detail;
};

template <typename T>
struct Result
{
    std::optional<T> value;
    AppError error;

    bool ok() const { return error.code == AppErrorCode::none; }
};

} // namespace app
} // namespace url_shortener
