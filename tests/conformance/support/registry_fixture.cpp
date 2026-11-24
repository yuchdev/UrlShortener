/**
 * @file registry_fixture.cpp
 * @brief Implementation of the per-case product registry fixture.
 */

#include <fstream>
#include <utility>

#include "registry_fixture.hpp"

#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/commands/job_runner.hpp"
#include "url_shortener/composition/job_runtime.hpp"
#include "url_shortener/config/storage_config.hpp"

namespace conformance
{

namespace
{

namespace app = url_shortener::app;
namespace composition = url_shortener::composition;

/// Writes @p content to @p path, truncating any existing file.
void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
}

/// Builds the StorageConfig matching @p yaml so the `system.info` snapshot
/// reports the same backend the YAML selects.
StorageConfig config_for(std::string_view backend,
                         const std::filesystem::path& db_path)
{
    StorageConfig sc;
    if (backend == "inmemory") {
        sc.metadata.backend = "inmemory";
    }
    else {
        sc.metadata.backend = "sqlite";
        sc.metadata.sqlite_path = db_path.string();
    }
    return sc;
}

}  // namespace

RegistryFixture::RegistryFixture(const std::filesystem::path& tmp,
                                 std::string_view backend,
                                 std::optional<JobRunnerKnob> job_knob)
{
    // Start each case from a clean process: drop any runner left by a prior
    // case (joining its workers) and clear the installed store.
    composition::reset_process_job_runtime_for_tests();
    app::LinkBackend::reset();

    // Pin the job runner's bounds before build_command_runtime creates it on
    // first use. Always configuring (even without an override) makes the bounds
    // deterministic regardless of what a prior case set, and the no-override
    // default equals `JobRunnerOptions{}`, i.e. the production V1 bounds, so
    // every corpus case behaves exactly as it does in `main`.
    composition::JobRuntimeSettings job_settings;
    if (job_knob.has_value()) {
        job_settings.runner.worker_count = job_knob->worker_count;
        job_settings.runner.queue_capacity = job_knob->queue_capacity;
    }
    composition::configure_process_job_runtime(job_settings);

    // Fixed base domain so a generated short_url is deterministic; slug length
    // stays at its default (matchers use <slug:N>).
    m_server_.shortener_base_domain = "sho.rt";

    const std::filesystem::path db_path = tmp / "links.db";
    const std::filesystem::path yaml_path = tmp / "storage.yaml";
    if (backend == "inmemory") {
        write_file(yaml_path, "metadata:\n  backend: inmemory\n");
    }
    else {
        std::error_code ec;
        std::filesystem::remove(db_path, ec);
        write_file(yaml_path,
                   "metadata:\n  backend: sqlite\n  sqlite_path: "
                       + db_path.string() + "\n");
    }
    m_server_.storage_config_path = yaml_path.string();

    // Publish a system.info snapshot matching the selected backend, exactly as
    // the Task 02.0 bootstrap does, so system.info would report the real
    // backend if a corpus case exercised it.
    app::set_process_system_info(
        app::make_system_info(config_for(backend, db_path), m_server_));

    // Install the per-case store and build the one production registry over it.
    m_backend_ = app::InitLinkBackend(m_server_);

    composition::CommandRuntimeOptions options;
    // Local-operator default (the direct transport maps to `cmd`/`tui`): grant
    // the file-path capability so import/export corpus cases that reference
    // ${tmp}/<file> are not refused `unsupported`. The capability is
    // independent of the caller role; a `user` caller still cannot use `path`
    // (the write raise is denied `not_authorized`).
    //
    // Confine every path to the per-case temp dir (defense in depth for the
    // corpus replay). A `path` arg is free text and, unlike a `files` key, is
    // not checked by the case runner; `cmd` ships no `--path-root`, so this
    // `root` is the only per-shim confinement available. With it, a case that
    // smuggles an absolute path or a `${tmp}/..` escape is refused
    // `invalid_field` and the out-of-root file is never opened or written.
    options.file_paths.allow_paths = true;
    options.file_paths.root = tmp.string();
    m_runtime_ = std::make_unique<composition::CommandRuntime>(
        composition::build_command_runtime(m_server_, m_backend_, options));
}

RegistryFixture::~RegistryFixture()
{
    // Teardown order mirrors the documented main() sequence: drop the runtime
    // (its link service borrows the store) -> reset the job runner (join
    // workers) -> reset the LinkBackend holder -> restore the default
    // system.info snapshot. The backend handle (a member) destructs last, after
    // the holder no longer references its store.
    m_runtime_.reset();
    composition::reset_process_job_runtime_for_tests();
    app::LinkBackend::reset();
    app::set_process_system_info(
        app::make_system_info(StorageConfig {}, ServerConfig {}));
}

url_shortener::commands::CommandRegistry& RegistryFixture::registry()
{
    return *m_runtime_->registry;
}

url_shortener::commands::JobRunner& RegistryFixture::job_runner()
{
    return *m_runtime_->job_runner;
}

url_shortener::commands::CallerContext RegistryFixture::caller(
    std::string_view role) const
{
    url_shortener::commands::CallerContext context;
    context.principal = "local-operator";
    context.transport = "direct";
    // Map only the exact corpus roles; fail closed to the least-privileged role
    // for anything else, so an unexpected token can never silently grant Admin.
    if (role == "admin") {
        context.control_set = ControlSet::Admin;
    }
    else {
        context.control_set = ControlSet::User;
    }
    return context;
}

}  // namespace conformance
