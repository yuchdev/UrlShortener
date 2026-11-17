#pragma once

#include <chrono>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string_view>

#include "url_shortener/commands/transport_adapter.hpp"

namespace url_shortener::commands
{
class JobRunner;
} // namespace url_shortener::commands

namespace url_shortener::composition
{

/**
 * @brief Construction-time options for a stream transport adapter built by
 * @ref make_transport_adapter.
 *
 * These couple the factory (composition code, not a transport-agnostic header)
 * to iostreams and @ref url_shortener::commands::JobRunner on purpose; the
 * seam kept pure is @ref url_shortener::commands::ITransportAdapter itself.
 *
 * @note The factory grants **no** filesystem (`path`) capability by default:
 * there is deliberately no option here that enables server file access. The
 * caller identity (`ControlSet`) and any filesystem capability an adapter
 * attaches to requests are an explicit, default-deny decision of the
 * composition root, never derived from the transport's own input (threat model
 * T-95, T-107).
 */
struct TransportOptions
{
    /**
     * @brief When `true`, the adapter downgrades the local caller from the
     * default `ControlSet::Admin` to `ControlSet::User` (read-only), as set by
     * `serve-stdio --read-only`. Default `false` (full local-operator rights).
     */
    bool read_only = false;

    /**
     * @brief Maximum length, in bytes, of a single input frame (NDJSON line)
     * the adapter will accept before rejecting it. Mirrors
     * `ServerConfig::max_request_body_bytes`. Default 65536.
     */
    std::size_t max_line_bytes = 65536;

    /**
     * @brief How long the adapter waits, on end-of-input, for in-flight jobs to
     * drain before forcing shutdown. Default 30 seconds.
     */
    std::chrono::milliseconds drain_timeout{30000};

    /**
     * @brief Process-wide job runner the adapter waits on at EOF to drain
     * in-flight jobs; may be `nullptr` when no job runtime is wired in, in
     * which case the adapter performs no drain. Non-owning: the pointed-to
     * runner must outlive the adapter.
     */
    commands::JobRunner* job_runner = nullptr;
};

/**
 * @brief Composition factory that creates one stream transport adapter by name
 * (ADR 0002 section 4; the single adapter-selection point, mirroring
 * `BuildStorageAdapters`).
 *
 * Only `"stdio"` is recognized in this milestone. The match is
 * case-sensitive; any other value (including an empty name) yields `nullptr`.
 * The returned adapter does not build its own registry: callers pass the
 * process-wide registry from `composition::build_command_runtime` to
 * @ref url_shortener::commands::ITransportAdapter::run (contract C11).
 *
 * @param name Lower-case transport identifier to construct; currently only
 * `"stdio"` is known.
 * @param options Construction-time bounds and wiring; see @ref TransportOptions.
 * @param in Input stream the adapter frames requests from. Must outlive the
 * returned adapter; not owned.
 * @param out Output stream the adapter writes response frames to. Must outlive
 * the returned adapter; not owned.
 * @return A newly constructed adapter owning `in`/`out` by reference, or
 * `nullptr` when @p name does not match a known transport.
 */
std::unique_ptr<commands::ITransportAdapter>
make_transport_adapter(std::string_view name, const TransportOptions& options,
                       std::istream& in, std::ostream& out);

} // namespace url_shortener::composition
