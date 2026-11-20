#pragma once

/**
 * @file registry_fixture.hpp
 * @brief Per-case product registry fixture for the direct conformance adapter.
 *
 * `RegistryFixture` builds the one fully-populated command registry every
 * transport executes against — through the single production entry point
 * `url_shortener::composition::build_command_runtime`, never by hand-wiring a
 * registry (milestone contract C11). It owns a per-case backing store (a fresh
 * `${tmp}/links.db` for the `sqlite` backend, a fresh in-memory metadata
 * repository for `inmemory`) and the process-wide job runner, resetting both at
 * construction and teardown so no state leaks between cases (subtask 04 success
 * criterion 2).
 *
 * The caller role is obtained exactly as a real local operator would: a
 * `CallerContext` whose `control_set` is `ControlSet::Admin` for `admin` and
 * `ControlSet::User` for `user` (standing threat brief, ruling (c)). The role
 * is never taken from a request's `args`/`options`; the closed envelope rejects
 * a `role`/`caller`/`read_only` key with `invalid_field`.
 *
 * Not thread-safe and process-global by construction: it drives the
 * process-wide `LinkBackend`, system-info and job-runner holders, so exactly
 * one fixture is alive at a time and cases run sequentially (the conformance
 * case runner does).
 */

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"

namespace conformance
{

/**
 * @brief Test-support override for the process-wide job runner's bounds.
 *
 * Supplied to @ref RegistryFixture only by a C++ runner that must drive a
 * job-queue edge the corpus cannot reach deterministically - notably the `busy`
 * error (queue full). Omitting it (the default) keeps the production V1 bounds,
 * so every corpus case behaves exactly as it does in `main`. The defaults here
 * mirror `commands::JobRunnerOptions` (two workers, a sixteen-deep queue).
 */
struct JobRunnerKnob
{
    ///< Always-on worker threads draining the queue (`>= 1`).
    std::size_t worker_count = 2;

    ///< Maximum number of *queued* (not yet running) jobs before `submit`
    ///< returns `busy` (`>= 1`).
    std::size_t queue_capacity = 16;
};

/**
 * @brief Owns the per-case product command registry and its backing store.
 *
 * Built through `composition::build_command_runtime` over a per-case backend,
 * so the conformance corpus exercises exactly the wiring `main` ships. The
 * members are declared in dependency order (the borrowed `ServerConfig` and
 * backend handle before the runtime that references them) so the runtime is
 * torn down first; see the destructor for the full teardown ordering.
 */
class RegistryFixture
{
public:
    /**
     * @brief Builds a fresh registry over a per-case backend.
     *
     * Resets the process-wide job runner and `LinkBackend` holders, installs a
     * backend selected by @p backend, publishes a matching `system.info`
     * snapshot, then builds the runtime. Base domain is fixed to `sho.rt` so a
     * generated `short_url` is deterministic across cases; the generated slug
     * length is left at its default (matchers compare it with `<slug:N>`).
     *
     * File-path capability follows the local-operator default the direct
     * transport maps to (`cmd`/`tui`): `allow_paths = true`, so corpus
     * `link.import`/`link.export` cases that reference `${tmp}/<file>` run
     * without being refused `unsupported`. The confinement root is pinned to
     * @p tmp (defense in depth for the corpus replay: a `path` arg is free text
     * and, unlike a `files` key, is not checked by the case runner, and `cmd`
     * ships no `--path-root`), so a case that smuggles an absolute path or a
     * `${tmp}/..` escape is refused `invalid_field` and the out-of-root file is
     * never opened or written. It does not opt in beyond that default; a `user`
     * caller still cannot use `path` because the path raise requires write (the
     * registry denies `not_authorized`).
     *
     * @param tmp The per-case temporary directory (already created by the case
     * runner). The `sqlite` database file and the storage-config YAML are
     * written under it, so the case runner's `remove_all(tmp)` cleans them up.
     * @param backend `"sqlite"` (fresh `${tmp}/links.db`) or `"inmemory"`
     * (fresh in-memory metadata repository). Any other value is treated as
     * `"sqlite"`.
     * @param job_knob Optional, test-support only: the job runner's worker
     * count and queue capacity. `std::nullopt` (the default) selects the
     * production V1 bounds, so every corpus case behaves identically; a value
     * is used only by a runner that must drive the `busy` (queue-full) edge
     * deterministically.
     * @throws std::runtime_error on an unrecoverable wiring failure from
     * `InitLinkBackend`/`build_command_runtime` (a bad path, a failed probe);
     * expected conditions never throw.
     */
    RegistryFixture(const std::filesystem::path& tmp,
                    std::string_view backend,
                    std::optional<JobRunnerKnob> job_knob = std::nullopt);

    /**
     * @brief Tears the fixture down and restores the process-wide default
     * state.
     *
     * Drops the runtime (destroying the registry and the services that borrow
     * the store) first, then drains and joins the job runner, resets the
     * `LinkBackend` holder, and restores the default `system.info` snapshot, so
     * the next case starts from a clean process. The backend handle (a member)
     * destructs last, after the holder no longer references its store.
     */
    ~RegistryFixture();

    RegistryFixture(const RegistryFixture&) = delete;
    RegistryFixture& operator=(const RegistryFixture&) = delete;
    RegistryFixture(RegistryFixture&&) = delete;
    RegistryFixture& operator=(RegistryFixture&&) = delete;

    /**
     * @brief Returns the per-case registry under test.
     * @return A reference to the populated registry; valid for the fixture's
     * lifetime.
     */
    url_shortener::commands::CommandRegistry& registry();

    /**
     * @brief Maps a corpus role to a real local-operator caller context.
     *
     * The caller role is the corpus `caller` only (ruling (c)); it is never
     * derived from a request body. The transport label is fixed to `"direct"`.
     *
     * @param role The corpus role: exactly `"admin"` selects
     * `ControlSet::Admin`; `"user"` and every other token (including the empty
     * string) fail closed to `ControlSet::User`, so an unexpected value can
     * never silently grant Admin.
     * @return A `CallerContext` carrying the resolved control set.
     */
    url_shortener::commands::CallerContext caller(std::string_view role) const;

    /**
     * @brief Returns the per-case process-wide job runner.
     *
     * Exposed for a runner that must occupy the queue directly (submit a
     * blocking body, then @ref url_shortener::commands::JobRunner::wait_idle to
     * drain on teardown) to reach a queue edge; corpus replay never needs it.
     *
     * @return A reference to the runner; valid for the fixture's lifetime.
     */
    url_shortener::commands::JobRunner& job_runner();

private:
    ///< Server config the runtime's link service borrows by reference (base
    ///< domain, slug length); declared before @ref m_runtime_ so it outlives
    ///< it.
    ServerConfig m_server_;

    ///< The per-case backend handle owning the installed store; declared before
    ///< @ref m_runtime_ so it is destroyed after it (the runtime's lister and
    ///< link service borrow the store it keeps installed).
    url_shortener::app::LinkBackendHandle m_backend_;

    ///< The composed runtime owning the registry and every object its handlers
    ///< reference. Held by pointer so the destructor can drop it explicitly,
    ///< before the job runner is reset and the backend holder released.
    std::unique_ptr<url_shortener::composition::CommandRuntime> m_runtime_;
};

}  // namespace conformance
