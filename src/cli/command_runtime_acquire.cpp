/**
 * @file command_runtime_acquire.cpp
 * @brief Shared one-shot runtime acquisition for the CLI command dispatchers.
 *
 * IMPORTANT (milestone contracts C4/C5): this translation unit must never start
 * the HTTP server, construct a Boost.Asio @c io_context, or touch
 * @c UriMapSingleton / `uri.txt`. It therefore includes nothing under
 * `url_shortener/http/` and no `boost/asio` headers.
 */
#include <url_shortener/cli/command_runtime_acquire.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <utility>

#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>

namespace url_shortener::cli
{
namespace
{

/// Type of the test-only fallback-build override (see
/// @ref detail::set_fallback_runtime_builder_for_testing).
using fallback_builder_t =
    std::function<std::shared_ptr<composition::CommandRuntime>(
        const ServerConfig&)>;

/// The process-wide storage slot for the test override. Null in production, so
/// the real `build_command_runtime` fallback runs. Not thread-safe; tests set
/// and clear it from a single thread.
fallback_builder_t& fallback_builder_slot()
{
    static fallback_builder_t slot;
    return slot;
}

}  // namespace

std::shared_ptr<composition::CommandRuntime> acquire_command_runtime(
    const ServerConfig& config)
{
    if (std::shared_ptr<composition::CommandRuntime> installed =
            composition::process_command_runtime())
    {
        return installed;
    }
    try {
        // Test seam: a configured override stands in for the real fallback
        // build so a test can exercise the null-runtime branch. Null in
        // production, where the real build runs.
        if (const fallback_builder_t& builder = fallback_builder_slot()) {
            return builder(config);
        }
        // Local-operator one-shot modes (`cmd`, `link <verb>`) opt into the
        // file-path capability so `link.import`/`link.export` may name a local
        // file (threat model T-95 default-deny; the REST runtime never takes
        // this path). In production `main` installs a process runtime first, so
        // this fallback is reached only by unit tests and tools.
        composition::CommandRuntimeOptions options;
        options.file_paths.allow_paths = true;
        return std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(
                config, app::LinkBackendHandle {}, options));
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

namespace detail
{

void set_fallback_runtime_builder_for_testing(fallback_builder_t builder)
{
    fallback_builder_slot() = std::move(builder);
}

}  // namespace detail

}  // namespace url_shortener::cli
