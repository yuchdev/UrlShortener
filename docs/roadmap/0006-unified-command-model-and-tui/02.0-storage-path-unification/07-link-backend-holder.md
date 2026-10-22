# 07 - LinkBackend holder and factory

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** none (needs only the existing `ILinkStore` / `LegacyLinkStore`)
**Blocks:** 08, 09, 11, 03.0/11
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Introduce the single seam through which the redirect handlers, the REST
handlers and the CLI obtain "the link store": a process-wide
`app::LinkBackend::store()` that returns the installed `ILinkStore` or, when
nothing is installed, the existing `LegacyLinkStore`. Re-point the existing
service factory at it. No handler is changed yet (08, 09); with nothing
installed behavior is identical to today.

## Dependencies

- `app::ILinkStore`, `app::LegacyLinkStore` (`include/url_shortener/app/legacy_adapters.hpp`,
  verified; `LegacyLinkStore` is stateless and delegates to the
  `linkRepository()` / `linkCache()` singletons).
- `app::BuildLegacyLinkCommandService` and `LinkCommandServiceBundle`
  (`include/url_shortener/composition/link_command_service_factory.hpp`,
  verified). Callers: `src/cli/link_command_dispatch.cpp`,
  `src/http/handlers/link_handlers.cpp::makeCommandService`,
  `tests/unit/cli/02_*`, `03_*` (they read only `bundle.service`).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/app/link_backend.hpp` | create | Holder API |
| `src/app/link_backend.cpp` | create | Implementation |
| `include/url_shortener/composition/link_command_service_factory.hpp` | modify | `store` member type becomes `std::shared_ptr<ILinkStore>`; document that the legacy name now means "current backend" |
| `src/composition/link_command_service_factory.cpp` | modify | Use `LinkBackend::store()` |
| `tests/unit/app/24_link_backend_holder.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/app/link_backend.cpp`; `APP_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::app {

class LinkBackend
{
public:
    /// The installed store, or the LegacyLinkStore default. Never null.
    /// Lock-free: one atomic pointer load (called on every redirect).
    static ILinkStore& store();

    /// Install the process-wide store. Call once, at startup, before any
    /// thread that serves requests exists. Replaces a previous installation.
    static void install(std::shared_ptr<ILinkStore> store);

    /// Remove the installed store (tests, shutdown after threads stopped).
    static void reset();

    static bool installed();
};
}
```

Implementation notes:

- `static std::atomic<ILinkStore*> s_current{nullptr}` for the lock-free read;
  `static std::shared_ptr<ILinkStore> s_owner` guarded by a `std::mutex` for
  `install`/`reset` only. `install` stores the owner first, then publishes the
  raw pointer with `memory_order_release`; `store()` uses `memory_order_acquire`.
- The default is a function-local `static LegacyLinkStore s_default`.
- `reset()` must not be called while redirect threads run (documented; startup
  and tests only).

Factory change:

```cpp
// LinkCommandServiceBundle
std::shared_ptr<ILinkStore> store;               // was shared_ptr<LegacyLinkStore>
// BuildLegacyLinkCommandService(config)
bundle.store = std::shared_ptr<ILinkStore>(std::shared_ptr<void>(), &LinkBackend::store());
// aliasing constructor with an empty owner: non-owning, valid while the holder lives
```

`BuildLegacyLinkCommandService` keeps its name (tests and two production call
sites use it); a doc comment records that it now binds to
`LinkBackend::store()`. `stats_reader` stays `LegacyLinkStatsReader`
(in-memory click repository; README R-3).

## Implementation steps

1. Add header and source.
2. Update the factory (header + source).
3. Write the tests; add them to `APP_UNIT_SOURCES`.
4. Build the whole project; run the CLI and HTTP unit suites to confirm no
   behavior change.

## Commands

```powershell
cmake --build cmake-build --target app__24_link_backend_holder
ctest --test-dir cmake-build -R "^app__24_link_backend_holder$" --output-on-failure
# no behavior change with nothing installed
ctest --test-dir cmake-build -R "^cli__0[1-4]_|^10_link_handlers$|^03_cli_success_output$" --output-on-failure
```

(`CLI_UNIT_SOURCES` targets are named `cli__01_...`; `10_link_handlers` comes
from `ALL_UNIT_SOURCES` and keeps its plain stem. Use `ctest -N` to confirm.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/24_link_backend_holder.cpp` | `app__24_link_backend_holder` | unit | below |

Each case calls `LinkBackend::reset()` in a fixture setup/teardown.

- Default: `installed() == false`; creating a link through
  `store().create(...)` makes it visible via `url_shortener::linkRepository()`
  (proves the default is the legacy singleton path).
- `install(fake)`: `installed() == true`, `store()` returns the fake; calls made
  through `BuildLegacyLinkCommandService(config).service->CreateLink(...)` reach
  the fake and not the legacy repository.
- `reset()` restores the default; `install` twice keeps the last.
- Owner lifetime: `install` with a `shared_ptr` whose last external reference is
  dropped keeps the store alive until `reset()` (weak_ptr expiry check).
- Concurrency smoke: 4 reader threads calling `store()` while one thread
  alternates `install`/`reset` 1000 times never observes null and never crashes
  (run under the unit label, < 2 s).
- Bundle store pointer is non-owning (copying/destroying bundles does not
  destroy the installed store).

## Constraints

- Hot path (C10): `store()` is one atomic load plus a branch; no locks, no
  allocation.
- Default behavior unchanged: no existing test may need modification (they
  seed `linkRepository()` and the default store reads it).
- Install-once-at-startup rule documented in the header (not enforced by a
  lock on the read side).
- C++17; `m_`/`s_` naming per repo style; no global mutable state other than the
  two statics inside `link_backend.cpp`.

## Success criteria

1. `app__24_link_backend_holder` passes.
2. `cli__01..04`, `03_cli_success_output`, `10_link_handlers` and the other
   existing unit suites pass unmodified.
3. `store()` contains no mutex (review/grep).

## Out of scope / risks

- Switching handlers to the holder (08, 09) and installing a store (11).
- Renaming `BuildLegacyLinkCommandService`; a rename is cosmetic and can follow
  once Task 03.0 replaces the factory with the command registry wiring.
- Using a global is a deliberate trade-off: handlers receive only
  `(req, config, is_tls, context)` (`handler_types.hpp`), so plumbing a store
  through them would change every handler signature and the router. A
  dependency-injected context is a larger refactor outside this milestone.
