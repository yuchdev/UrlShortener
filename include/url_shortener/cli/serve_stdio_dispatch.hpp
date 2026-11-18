/**
 * @file serve_stdio_dispatch.hpp
 * @brief One-shot process entry for the `serve-stdio` NDJSON transport mode
 * (Task 05.0 subtask 17).
 *
 * `url_shortener serve-stdio` runs the local-operator NDJSON stream transport:
 * one request envelope per stdin line, one response envelope per stdout line,
 * end-of-input ends the session. This header declares the two entry points
 * `main()` dispatches to; it forward declares the only two types it needs and
 * pulls in nothing under `url_shortener/http/`, no Boost.Asio/Beast, and never
 * starts the network server or touches `uri.txt`/`UriMapSingleton` (milestone
 * contracts C5/C11). `<iosfwd>` is the only standard include, for the injected
 * stream references of @ref url_shortener::cli::RunServeStdioOn.
 */
#pragma once

#include <iosfwd>

/// Top-level server configuration (defined in url_shortener/core/config.hpp).
struct ServerConfig;

/// Parsed `serve-stdio` hardening options `--read-only` / `--no-file-paths` /
/// `--path-root` (defined in url_shortener/cli/cli_parser.hpp).
struct ServeStdioCliOptions;

namespace url_shortener::app
{
/// The process-wide storage handle `main` installed (active under
/// `--storage-config`, inactive for the legacy in-memory default); defined in
/// url_shortener/composition/link_backend_bootstrap.hpp. Taken by const
/// reference so this seam header needs no storage definitions.
class LinkBackendHandle;
}  // namespace url_shortener::app

namespace url_shortener::cli
{
/**
 * @brief Runs one `serve-stdio` session over the given streams and returns a
 * process exit code (the testable seam: streams injected, no process-global
 * side effect other than the process-wide job runtime).
 *
 * Obtains the command runtime from the one composition root
 * (`composition::build_command_runtime`, milestone contract C11) over @p backend
 * — the same active handle `main` installed, so the browse lister
 * (`link.list`/`link.export`/`db.rows links`) reads the same configured
 * repository that `link.create`/`update`/`delete` write through, with no split
 * store under `--storage-config` — with the file-path capability derived from
 * @p options (`allow_paths = !no_file_paths`, `root = path_root`; threat model
 * T-95 default-deny is lifted here because the local operator owns the
 * filesystem). It then creates the stdio adapter through
 * `composition::make_transport_adapter`, runs it against the one registry, and
 * drains and joins the process-wide job runtime. It constructs no
 * `CommandRegistry`, `JobRunner` or service bundle of its own, builds no
 * `HttpServer`, binds no socket, and never touches `uri.txt`/`UriMapSingleton`
 * (contracts C5/C11).
 *
 * Caller setup it does **not** perform (owned by @ref RunServeStdio before the
 * first read/write): `sync_with_stdio(false)`, the POSIX `SIGPIPE` ignore, the
 * Windows binary-mode switch, and forcing the logger onto `LogSink::Stderr`.
 * A caller that drives this directly (the unit tests) must initialize logging
 * so no log line reaches @p out; the adapter guards that its sink is never
 * `LogSink::Stdout` (threat model T-102).
 *
 * @param config Server configuration the built runtime borrows by reference
 * (base domain, slug length, `max_request_body_bytes` for the line cap); must
 * outlive the call.
 * @param options The parsed `serve-stdio` hardening flags: `read_only`
 * downgrades the local caller to `ControlSet::User`; `no_file_paths` and
 * `path_root` shape the file-path capability.
 * @param backend The process-wide storage handle; its `repository()` selects the
 * browse lister (active handle -> `RepositoryLinkLister` over the configured
 * backend, inactive handle -> the legacy in-memory lister). Must outlive the
 * call. Tests pass an inactive or an active handle through this seam.
 * @param in Untrusted request source, one NDJSON envelope per line; never
 * logged. Must outlive the call.
 * @param out Response sink; only response envelopes are written here (contract
 * C2). Must outlive the call.
 * @return `0` on a clean end-of-input shutdown after any in-flight jobs drain;
 * `2` when `--path-root` names a path that is not an existing directory (one
 * stderr line, before any input is read); `4` on a hard input error, an
 * output-stream failure, an escaping exception, or a runtime-build failure.
 */
int RunServeStdioOn(const ServerConfig& config,
                    const ServeStdioCliOptions& options,
                    const app::LinkBackendHandle& backend,
                    std::istream& in,
                    std::ostream& out);

/**
 * @brief Process entry for `serve-stdio`: applies the platform and logging
 * setup, then runs @ref RunServeStdioOn over `std::cin`/`std::cout`.
 *
 * Invoked from `main()` when `ParseResult::serve_stdio` is engaged, before any
 * `HttpServer` is constructed (contract C5). Setup, in order, before the first
 * read or write:
 * 1. `std::ios::sync_with_stdio(false); std::cin.tie(nullptr);` (no iostream
 *    exception masks are enabled, so a broken stream fails rather than throws).
 * 2. POSIX: ignore `SIGPIPE` so a closed stdout becomes a stream failure
 *    (exit `4`) instead of terminating the process (threat model T-106).
 *    Windows: switch stdin/stdout to binary mode so `\n` is not rewritten and
 *    Ctrl-Z does not end input.
 * 3. Force the process logger to `enabled = true`, `sink = LogSink::Stderr`
 *    regardless of any configuration (`LogSink::Stdout` is never allowed in this
 *    mode), **before** the job runtime and its workers are built, so stdout
 *    carries response envelopes only and the audit trail goes to stderr for the
 *    parent to capture (decision log D14, threat model T-102).
 *
 * @param config Server configuration forwarded to @ref RunServeStdioOn; must
 * outlive the call.
 * @param options The parsed `serve-stdio` hardening flags forwarded unchanged.
 * @param backend The process-wide storage handle `main` installed, forwarded to
 * @ref RunServeStdioOn so the lister reads the configured backend. Must outlive
 * the call.
 * @return The exit code of @ref RunServeStdioOn over the process streams.
 */
int RunServeStdio(const ServerConfig& config,
                  const ServeStdioCliOptions& options,
                  const app::LinkBackendHandle& backend);

}  // namespace url_shortener::cli
