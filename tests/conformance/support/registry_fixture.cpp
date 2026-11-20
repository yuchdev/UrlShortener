/**
 * @file registry_fixture.cpp
 * @brief Implementation of the per-case product registry fixture.
 */

#include <fstream>
#include <utility>

#include "registry_fixture.hpp"

#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/system_info.hpp"
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
                                 std::string_view backend)
{
    // Start each case from a clean process: drop any runner left by a prior
    // case (joining its workers) and clear the installed store.
    composition::reset_process_job_runtime_for_tests();
    app::LinkBackend::reset();

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
    // the file-path capability, full filesystem, so import/export corpus cases
    // that reference ${tmp}/<file> are not refused `unsupported`. The
    // capability is independent of the caller role; a `user` caller still
    // cannot use `path` (the write raise is denied `not_authorized`).
    options.file_paths.allow_paths = true;
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

url_shortener::commands::CallerContext RegistryFixture::caller(
    std::string_view role) const
{
    url_shortener::commands::CallerContext context;
    context.principal = "local-operator";
    context.transport = "direct";
    context.control_set =
        (role == "user") ? ControlSet::User : ControlSet::Admin;
    return context;
}

}  // namespace conformance
