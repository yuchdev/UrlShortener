#pragma once

#include <memory>

#include <url_shortener/app/link_command_service.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Process-wide holder for "the link store" — the single seam
 * through which the redirect handlers, the REST handlers and the CLI
 * obtain an `ILinkStore`.
 *
 * `store()` returns the installed `ILinkStore` or, when nothing has been
 * installed, the stateless `LegacyLinkStore` default (which delegates to
 * the global `linkRepository()` / `linkCache()` singletons). With nothing
 * installed, behavior is byte-for-byte identical to the pre-holder code
 * path (Task 02.0 README decision 1).
 *
 * The holder owns the installed store via an internal `std::shared_ptr`
 * for as long as it remains installed. Non-owning references and aliasing
 * `std::shared_ptr`s handed out by callers (e.g. the service factory)
 * therefore stay valid while the holder keeps the store installed, even
 * if every external owner is dropped.
 *
 * ## Lifetime and thread-safety contract (read before calling)
 * - `store()` is **lock-free**: a single acquiring atomic load plus a
 *   branch, with no mutex and no allocation. It is safe to call
 *   concurrently from every request-handling thread and is the only
 *   method intended for the redirect hot path (shared contract C10).
 * - `install()` and `reset()` are **startup/teardown only**. They take an
 *   internal mutex to swap the owner but **must not** be called while any
 *   thread that serves requests is running: a concurrent `store()` may
 *   already hold the returned `ILinkStore&`, and tearing the store down
 *   underneath it is a use-after-free (threat model T-39). Call
 *   `install()` once, before the first request-serving thread exists; call
 *   `reset()` only after all such threads have stopped (and in tests).
 *   This install-once-at-startup rule is a documented contract, not
 *   enforced by a lock on the read side.
 * - Exactly one store is installed per process/mode (shared contract C11).
 */
class LinkBackend
{
public:
    /**
     * @brief Returns the currently installed store, or the
     * `LegacyLinkStore` default when nothing is installed. Never null.
     *
     * Lock-free: one acquiring atomic load plus a branch; no mutex, no
     * allocation. Safe to call from every request thread, including the
     * redirect fast path (C10). The returned reference is valid until a
     * subsequent `install()`/`reset()`, which the contract above forbids
     * while request threads run.
     * @return A reference to the active `ILinkStore`.
     */
    static ILinkStore& store();

    /**
     * @brief Installs the process-wide store, replacing any previous
     * installation and taking shared ownership of @p store for as long as
     * it stays installed.
     *
     * Call exactly once, at startup, before any request-serving thread
     * exists (see the class-level contract). Takes an internal mutex;
     * publishes the raw pointer with release ordering so a later
     * acquiring `store()` observes a fully constructed store. Passing a
     * null @p store is equivalent to `reset()`.
     * @param store The store to install; shared ownership is retained by
     * the holder until the next `install()`/`reset()`.
     */
    static void install(std::shared_ptr<ILinkStore> store);

    /**
     * @brief Removes the installed store and drops the holder's ownership
     * of it, restoring the `LegacyLinkStore` default.
     *
     * Startup/teardown and tests only; must not run while redirect threads
     * are active (see the class-level contract). Takes the internal mutex,
     * clears the published pointer first (release ordering), then releases
     * the owning `std::shared_ptr`.
     */
    static void reset();

    /**
     * @brief Reports whether a store has been installed.
     * @return `true` if `install()` installed a non-null store that has
     * not since been `reset()`; `false` when the `LegacyLinkStore` default
     * is active.
     */
    static bool installed();
};

}  // namespace app
}  // namespace url_shortener
