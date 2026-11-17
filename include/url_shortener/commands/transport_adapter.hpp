#pragma once

#include <string_view>

namespace url_shortener::commands
{

class CommandRegistry;

/**
 * @brief A stream transport that owns its own receive/execute/respond loop
 * (ADR 0002 section 4).
 *
 * An adapter has exactly three responsibilities and no others: *frame in*
 * (turn the transport's native input into a @ref CommandRequest), *call*
 * @ref CommandRegistry::execute, and *frame out* (turn the resulting
 * @ref CommandResponse into the transport's native output). It contains no
 * business rules, no per-command logic, and no DTO or error mapping (milestone
 * contracts C1/C2); those all live behind the registry.
 *
 * Only stream transports that own their loop implement this interface. In this
 * milestone that is the stdio NDJSON adapter; a future IPC adapter would be the
 * next. REST and the one-shot CLI are deliberately *not* expressed as
 * @ref ITransportAdapter (decision log D7): REST is a route handler inside the
 * existing `HttpServer`/`Router`, and the CLI is a one-shot function called
 * from `main`.
 *
 * **Lifetime / threading contract.** @ref run owns the adapter's loop for the
 * whole duration of the transport and must be callable **exactly once** per
 * instance; a second call is a programming error. An adapter is not required to
 * be thread-safe and is expected to be driven by a single thread. The
 * @ref CommandRegistry handed to @ref run must outlive the @ref run call.
 *
 * **Caller identity is chosen by composition, not by this interface.** The
 * @ref CallerContext (its `control_set` and filesystem capability) that an
 * adapter attaches to each request is fixed when the adapter is constructed in
 * `src/composition/`, never read from or escalated by the transport's own input
 * (threat model T-107). No adapter grants a filesystem (`path`) capability by
 * default; such a capability is an explicit, default-deny choice of the
 * composition root (threat model T-95).
 *
 * **No listener in non-network modes (contract C5).** Unless an adapter is
 * explicitly a network transport, @ref run must never construct `HttpServer`,
 * bind a socket, or touch `uri.txt`/`UriMapSingleton`.
 */
class ITransportAdapter
{
public:
    /**
     * @brief Virtual destructor; instances are owned through a base pointer.
     */
    virtual ~ITransportAdapter() = default;

    /**
     * @brief The stable, lower-case identifier of this transport.
     *
     * @return A view of the adapter's name (for example `"stdio"`, later
     * `"ipc"`). The referenced storage must outlive the adapter; implementers
     * typically return a view of a string literal or a member string.
     */
    virtual std::string_view name() const = 0;

    /**
     * @brief Runs the transport's receive/execute/respond loop until its input
     * stream ends, then returns a process exit code.
     *
     * Owns the loop for the whole lifetime of the transport. Must be callable
     * exactly once per instance; calling it a second time is a programming
     * error. Must not construct `HttpServer`, bind a socket, or touch `uri.txt`
     * (contract C5) unless the adapter is explicitly a network transport.
     *
     * @param registry The process-wide command registry (built by
     * `composition::build_command_runtime`; adapters never build their own,
     * contract C11) against which every framed request is executed. Must
     * outlive this call.
     * @return `0` on a clean shutdown (for stdio, EOF on input after any
     * in-flight work drains); otherwise a catalog CLI exit code: `2` for a
     * usage error, `4` for an internal error.
     */
    virtual int run(CommandRegistry& registry) = 0;
};

} // namespace url_shortener::commands
