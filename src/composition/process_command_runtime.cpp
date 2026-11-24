#include <cassert>
#include <memory>
#include <utility>

#include "url_shortener/composition/command_runtime.hpp"

#include "url_shortener/app/link_backend.hpp"

namespace url_shortener
{
namespace composition
{

namespace
{

/**
 * @brief The published process-wide command runtime, or null when none is
 * installed (the "transport falls back to its own default" signal).
 *
 * Accessed exclusively through the `std::atomic_*` free functions for
 * `std::shared_ptr` (C++17), so a request thread's @ref process_command_runtime
 * load and a teardown thread's @ref install_process_command_runtime /
 * @ref reset_process_command_runtime store never form a data race and a reader
 * always observes a fully published runtime. Returning an owning copy means a
 * concurrent reset can never dangle an in-flight reader (threat model T-39 for
 * the store applies here too, but the shared_ptr copy makes the read path
 * memory-safe without the serving latch doing the work).
 */
std::shared_ptr<CommandRuntime> s_process_runtime;

}  // namespace

void install_process_command_runtime(std::shared_ptr<CommandRuntime> runtime)
{
    // Install-once-at-startup contract: never publish a runtime once request
    // threads (which load it lock-free) may be running. Reuses the one serving
    // latch app::LinkBackend already latches in main before the first
    // request-serving thread.
    assert(!app::LinkBackend::serving()
           && "install_process_command_runtime() called after mark_serving()");
    std::atomic_store(&s_process_runtime, std::move(runtime));
}

std::shared_ptr<CommandRuntime> process_command_runtime()
{
    return std::atomic_load(&s_process_runtime);
}

void reset_process_command_runtime()
{
    // Teardown only. Deliberately NOT guarded by the serving latch: main resets
    // the runtime while the latch is still set (before LinkBackend::shutdown()
    // clears it), because the runtime must drop its borrow of the store/
    // repository before the backend handle tears them down. The atomic store is
    // itself race-free against a concurrent process_command_runtime() read.
    std::atomic_store(&s_process_runtime, std::shared_ptr<CommandRuntime> {});
}

}  // namespace composition
}  // namespace url_shortener
