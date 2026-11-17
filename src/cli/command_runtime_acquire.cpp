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
#include <memory>

#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>

namespace url_shortener::cli
{

std::shared_ptr<composition::CommandRuntime> acquire_command_runtime(
    const ServerConfig& config)
{
    if (std::shared_ptr<composition::CommandRuntime> installed =
            composition::process_command_runtime())
    {
        return installed;
    }
    try {
        return std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(config,
                                               app::LinkBackendHandle {}));
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace url_shortener::cli
