/**
 * @file serve_stdio_dispatch.cpp
 * @brief Process setup, runtime lookup, run and shutdown for the `serve-stdio`
 * NDJSON transport mode (Task 05.0 subtask 17).
 *
 * IMPORTANT (milestone contracts C5/C11): this translation unit must never start
 * the HTTP server, construct a Boost.Asio @c io_context, bind a socket, build a
 * @c CommandRegistry / @c JobRunner / service bundle of its own, or touch
 * @c UriMapSingleton / `uri.txt`. It therefore includes nothing under
 * `url_shortener/http/` and no `boost/asio` headers. All business rules, DTO
 * mapping and authorization stay behind the one @ref
 * url_shortener::commands::CommandRegistry obtained from
 * `composition::build_command_runtime`; this file only sets the process up,
 * frames in/out through the adapter, and drains the process-wide job runtime.
 */
#include <url_shortener/cli/serve_stdio_dispatch.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

#include <url_shortener/app/system_info.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/transport_adapter.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/composition/transport_factory.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/observability/logging_config.hpp>

namespace url_shortener::cli
{
namespace
{

/// How long the EOF drain waits for in-flight jobs before cancelling the rest
/// (ADR 0002 section 4 default; the adapter also bounds the wait).
constexpr std::chrono::seconds k_drain_timeout{30};

/// Final bounded join of the process-wide runner after the adapter already
/// drained cooperatively (spec step 5): workers are idle by now, so this returns
/// promptly on every realistic (cancel-polling) body.
constexpr std::chrono::seconds k_shutdown_timeout{5};

}  // namespace

int RunServeStdioOn(const ServerConfig& config,
                    const ServeStdioCliOptions& options,
                    const app::LinkBackendHandle& backend,
                    std::istream& in,
                    std::ostream& out)
{
    // Step 1: `--path-root`, when given, must name an existing directory, checked
    // before any input is read. The value-free diagnostic never echoes the path
    // (contract C6); the exit code is the shared table's `invalid_field` code (2),
    // read from the table so this unit stays consistent with the command layer.
    if (options.path_root.has_value()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(*options.path_root, ec)) {
            std::cerr << "serve-stdio: --path-root is not an existing "
                         "directory\n";
            return commands::error_info_for(
                       commands::CommandErrorCode::invalid_field)
                .exit_code;
        }
    }

    // Step 2: the one composition root (contract C11). The file-path capability
    // is the local operator's (threat model T-95 default-deny is lifted here):
    // `allow_paths = !--no-file-paths`, confined to `--path-root` when given. No
    // designated initializers (C++17): assign the members. The runtime is built
    // over @p backend — the same active handle `main` installed — so the browse
    // lister reads the configured repository that `link.create`/`update`/`delete`
    // write through; a stale/empty `link.list` under `--storage-config` (the
    // split store bug) is thereby avoided.
    composition::CommandRuntimeOptions runtime_options;
    runtime_options.file_paths.allow_paths = !options.no_file_paths;
    runtime_options.file_paths.root = options.path_root;

    // Startup record (stderr only, D14): version and the allowlisted backend
    // names system.info reports, plus the hardening policy. Every field is
    // value-free — no paths, DSNs, addresses or peers (contract C6; the
    // SystemInfo backend fields are drawn from fixed per-section allowlists).
    const app::SystemInfo info = app::process_system_info();
    const auto logger = observability::LoggerFactory::Component("serve_stdio");
    logger.info("serve_stdio_start",
                {{"version", info.version},
                 {"metadata_backend", info.metadata},
                 {"cache_backend", info.cache},
                 {"analytics_backend", info.analytics},
                 {"rate_limit_backend", info.rate_limit},
                 {"read_only", options.read_only ? "true" : "false"},
                 {"file_paths",
                  runtime_options.file_paths.allow_paths ? "allowed"
                                                         : "denied"},
                 {"path_root",
                  options.path_root.has_value() ? "set" : "unset"}});

    composition::CommandRuntime runtime;
    try {
        runtime =
            composition::build_command_runtime(config, backend, runtime_options);
    }
    catch (const std::exception&) {
        // An unrecoverable wiring failure (reserved for programming/startup
        // errors by the repo rule). Value-free diagnostic; internal exit (4).
        std::cerr << "serve-stdio: failed to build command runtime\n";
        logger.info("serve_stdio_stop",
                    {{"exit", "4"}, {"responses", "0"}});
        return commands::error_info_for(commands::CommandErrorCode::internal)
            .exit_code;
    }

    // Step 3: the stream transport. The line cap mirrors the request body limit;
    // the non-owning job-runner pointer lets the adapter drain in-flight jobs at
    // EOF (the shared process-wide runner of subtask 20).
    composition::TransportOptions transport_options;
    transport_options.read_only = options.read_only;
    transport_options.max_line_bytes = config.max_request_body_bytes;
    transport_options.drain_timeout = k_drain_timeout;
    transport_options.job_runner = runtime.job_runner.get();

    // Step 4: build the adapter through the single adapter-selection point and
    // run the session to a terminal condition. `make_transport_adapter` only
    // returns null for an unknown transport name; `"stdio"` is always known.
    const std::unique_ptr<commands::ITransportAdapter> adapter =
        composition::make_transport_adapter("stdio", transport_options, in,
                                            out);
    const int rc = adapter->run(*runtime.registry);

    // Step 5: the adapter already drained (and, for a cooperative body, joined)
    // the runner; this bounded call joins any remaining workers so no thread
    // outlives the process. Idempotent after the adapter's own `shutdown()`.
    composition::shutdown_process_job_runtime(k_shutdown_timeout);

    logger.info(
        "serve_stdio_stop",
        {{"exit", std::to_string(rc)},
         {"responses", std::to_string(adapter->responses_written())}});
    return rc;
}

int RunServeStdio(const ServerConfig& config,
                  const ServeStdioCliOptions& options,
                  const app::LinkBackendHandle& backend)
{
    // Step 1: decouple C and C++ streams for throughput and stop `std::cin`
    // flushing `std::cout`; no iostream exception masks are enabled, so a broken
    // stream fails (and is mapped to exit 4) rather than throwing (carry-forward
    // 5). The streams keep their default (unmasked) exception state.
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

#if defined(_WIN32)
    // Binary mode so `\n` is not rewritten to `\r\n` on stdout and Ctrl-Z in the
    // input is a normal byte rather than an end-of-file marker.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    // Carry-forward 4 / threat model T-106: ignore SIGPIPE so writing to a closed
    // stdout sets the stream's failbit (-> `output_failed` -> exit 4) instead of
    // delivering a signal whose default disposition kills the process.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Step 3 / decision log D14 / threat model T-102 (carry-forward 1): force the
    // logger onto stderr BEFORE the job runtime and its workers exist, so stdout
    // carries only response envelopes and the audit trail (the runtime's
    // LoggerAuditSink) goes to stderr for the parent to capture. `LogSink::Stdout`
    // is never allowed in this mode; `LogSink::Stderr` always passes validation,
    // so this initialization cannot fail.
    observability::LoggingConfig log_cfg;
    log_cfg.enabled = true;
    log_cfg.sink = observability::LogSink::Stderr;
    observability::LoggerFactory::Initialize(log_cfg);

    return RunServeStdioOn(config, options, backend, std::cin, std::cout);
}

}  // namespace url_shortener::cli
