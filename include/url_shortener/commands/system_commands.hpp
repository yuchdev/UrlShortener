#pragma once

#include <url_shortener/commands/command_descriptor.hpp>

namespace url_shortener
{
namespace app
{

// Forward declaration: `make_system_info_descriptor` takes an immutable
// @ref SystemInfo snapshot by value. A by-value parameter needs the complete
// type only at the function's definition and call sites, not at this
// declaration, so forward-declaring it keeps this public header free of
// `app/system_info.hpp` (which pulls in the config aggregates). The full
// definition lives in `app/system_info.hpp`, included by `system_commands.cpp`
// and the tests.
struct SystemInfo;

}  // namespace app
}  // namespace url_shortener

namespace url_shortener::commands
{

/**
 * @brief Builds the `system.info` command descriptor (catalog 3.2).
 *
 * The one definition of the "which backend am I talking to / what version"
 * operation (milestone contract C1): the returned descriptor is `read`, `sync`,
 * non-`mutating`, does not support `dry_run`, and takes no arguments. Its
 * handler captures the immutable @p info snapshot by value, derives `read_only`
 * from the calling `ControlSet` (`read_only = !canWrite(caller)`), and
 * serializes the result with `app::serializeSystemInfoJson`. Because the
 * snapshot is captured by value the result is stable across calls and reads no
 * live config, so the handler discloses only allowlisted backend names and the
 * explicitly-named non-secret `ServerConfig` subset (milestone contract C6,
 * threat model T-53).
 *
 * Because an empty argument schema is a closed schema, any supplied argument is
 * rejected `invalid_field` by the registry before the handler runs, and a
 * `dry_run` request yields `unsupported` (the command is not
 * `supports_dry_run`) - the handler never sees either case.
 *
 * Unlike the link/db factories this binds no live service: the snapshot is
 * self-contained, so the descriptor is safe to execute from any composition
 * root and has no lifetime dependency beyond the registry that stores it.
 *
 * @param info The immutable system snapshot the handler serializes; copied into
 * the descriptor so it needs no external owner.
 * @return A fully populated @ref CommandDescriptor ready for
 * @ref CommandRegistry::add.
 */
CommandDescriptor make_system_info_descriptor(app::SystemInfo info);

}  // namespace url_shortener::commands
