#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/app/system_info.hpp>
#include <url_shortener/app/table_source.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/browse_commands.hpp>
#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/job_commands.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/link_commands.hpp>
#include <url_shortener/commands/pair_commands.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Serializes one @ref ArgSpec to its public catalog JSON object.
 *
 * Emits `{"name","type","required"[,"enum":[...]][,"default":<token>],"doc"}`
 * in that fixed key order. `"enum"` appears only for an @ref
 * ArgType::enumeration argument (its values escaped through `jsonString`);
 * `"default"` appears only when @ref ArgSpec::default_value is set and is
 * embedded verbatim as the descriptor's trusted raw JSON token. No other
 * @ref ArgSpec field (bounds, nullability, nesting, grouping) is part of the
 * catalog shape, so none is emitted (threat model T-20).
 *
 * @param arg The argument spec to serialize.
 * @return One well-formed JSON object as text.
 */
/**
 * @brief Never-executed placeholder @ref app::ILinkLister for the documented
 * registry (@ref register_documented_commands).
 *
 * The catalog generator renders descriptor metadata only and never calls
 * @ref CommandRegistry::execute (threat model T-19), so this `list` body is
 * unreachable in practice; it returns a fixed `internal` error should anything
 * ever call it, surfacing the invariant violation instead of touching storage.
 */
class DocPlaceholderLinkLister final : public app::ILinkLister
{
public:
    /**
     * @brief Returns a fixed `internal` error; never invoked in the docs path.
     * @param query Ignored.
     * @return A failed @ref app::Result with code `internal`.
     */
    app::Result<app::LinkListPage> list(
        const app::LinkListQuery& query) const override
    {
        (void)query;
        app::Result<app::LinkListPage> result;
        result.error = app::AppError {app::AppErrorCode::internal,
                                      "documentation placeholder"};
        return result;
    }
};

/**
 * @brief Never-executed placeholder @ref app::ILinkStore for the documented
 * registry (@ref register_documented_commands).
 *
 * The documented link descriptors exist only so the catalog generator and the
 * drift test can read their metadata; that path never calls
 * @ref CommandRegistry::execute (threat model T-19/T-58). Binding the live link
 * descriptors to a store over the process-global `linkRepository()` would let an
 * accidental `execute()` on the documented registry mutate the real store, so
 * every method here performs no I/O and fails closed instead: writes
 * (@ref create, @ref update) report a fixed `internal` error, lookups
 * (@ref findBySlug, @ref findById) return @c std::nullopt, @ref slugExists
 * returns @c false, and @ref invalidateCache is a no-op. It must therefore never
 * be executed; the failures only surface the invariant violation.
 */
class DocPlaceholderLinkStore final : public app::ILinkStore
{
public:
    /**
     * @brief Fails closed with a fixed `internal` error; never invoked on the
     * docs path and writes nothing.
     * @param link Ignored.
     * @param[out] error If non-null, receives @ref app::AppErrorCode::internal
     * with the fixed `"documentation placeholder"` detail; left untouched when
     * null.
     * @return Always `false`.
     */
    bool create(const Link& link, app::AppError* error = nullptr) override
    {
        (void)link;
        fail(error);
        return false;
    }

    /**
     * @brief Returns @c std::nullopt; never invoked on the docs path and reads
     * nothing.
     * @param slug Ignored.
     * @return Always @c std::nullopt.
     */
    std::optional<Link> findBySlug(const std::string& slug) const override
    {
        (void)slug;
        return std::nullopt;
    }

    /**
     * @brief Returns @c std::nullopt; never invoked on the docs path and reads
     * nothing.
     * @param id Ignored.
     * @return Always @c std::nullopt.
     */
    std::optional<Link> findById(const std::string& id) const override
    {
        (void)id;
        return std::nullopt;
    }

    /**
     * @brief Returns @c false; never invoked on the docs path and reads nothing.
     * @param slug Ignored.
     * @return Always @c false (no slug is ever reported as present).
     */
    bool slugExists(const std::string& slug) const override
    {
        (void)slug;
        return false;
    }

    /**
     * @brief No-op; never invoked on the docs path and caches nothing.
     * @param slug Ignored.
     */
    void invalidateCache(const std::string& slug) override { (void)slug; }

    /**
     * @brief Fails closed with a fixed `internal` error; never invoked on the
     * docs path and writes nothing.
     * @param link Ignored.
     * @param[out] error If non-null, receives @ref app::AppErrorCode::internal
     * with the fixed `"documentation placeholder"` detail; left untouched when
     * null.
     * @return Always `false`.
     */
    bool update(const Link& link, app::AppError* error = nullptr) override
    {
        (void)link;
        fail(error);
        return false;
    }

private:
    /**
     * @brief Writes the fixed `internal` placeholder error to @p error when it
     * is non-null.
     * @param[out] error Optional out-param to populate; left untouched when
     * null.
     */
    static void fail(app::AppError* error)
    {
        if (error != nullptr) {
            *error = app::AppError {app::AppErrorCode::internal,
                                    "documentation placeholder"};
        }
    }
};

/**
 * @brief Never-executed placeholder @ref app::ILinkStatsReader for the
 * documented registry (@ref register_documented_commands).
 *
 * Mirrors @ref DocPlaceholderLinkStore: the documented `link.stats` descriptor
 * is metadata-only and never executed (T-19/T-58), so @ref read performs no I/O
 * and fails closed with a fixed `internal` error rather than touching the
 * process-global analytics store. It must never be executed.
 */
class DocPlaceholderLinkStatsReader final : public app::ILinkStatsReader
{
public:
    /**
     * @brief Returns a fixed `internal` error; never invoked on the docs path.
     * @param query Ignored.
     * @return A failed @ref app::Result with code
     * @ref app::AppErrorCode::internal.
     */
    app::Result<app::LinkStatsView> read(
        const app::GetLinkStatsQuery& query) const override
    {
        (void)query;
        app::Result<app::LinkStatsView> result;
        result.error = app::AppError {app::AppErrorCode::internal,
                                      "documentation placeholder"};
        return result;
    }
};

/**
 * @brief The process-lifetime placeholder link service the documented link
 * descriptors bind to.
 *
 * Built over a @ref DocPlaceholderLinkStore / @ref DocPlaceholderLinkStatsReader
 * and a default `ServerConfig`: every port method is inert and fails closed, so
 * even an accidental `execute()` on the documented registry does no I/O and
 * cannot reach the process-global `linkRepository()` or analytics store (threat
 * model T-19/T-58). The statics live for the program lifetime so any registry
 * @ref register_documented_commands populates can reference them safely. This
 * service must never be executed.
 * @return A reference to the shared placeholder service.
 */
app::LinkCommandService& doc_link_service()
{
    static DocPlaceholderLinkStore store;
    static DocPlaceholderLinkStatsReader stats;
    static const ServerConfig config;
    static app::LinkCommandService service(store, stats, config);
    return service;
}

/**
 * @brief The process-lifetime placeholder browse lister.
 * @return A reference to the shared placeholder lister.
 */
app::ILinkLister& doc_lister()
{
    static DocPlaceholderLinkLister lister;
    return lister;
}

/**
 * @brief The process-lifetime placeholder (empty) table catalog the documented
 * `db.*` descriptors bind to; their metadata does not depend on its contents.
 * @return A reference to the shared placeholder catalog.
 */
app::TableCatalog& doc_catalog()
{
    static app::TableCatalog catalog;
    return catalog;
}

/**
 * @brief The process-lifetime placeholder @ref JobRunner the documented
 * `job.*` / `link.import` / `link.export` descriptors bind to.
 *
 * The documented registry renders metadata only and never executes a handler
 * (threat model T-19/T-58), so this runner's workers stay idle; it exists solely
 * so `register_job_commands`/`register_pair_commands` can capture a runner
 * reference while building descriptors. Process-lifetime statics (the clock
 * declared before the runner so it outlives it) keep the reference valid for any
 * registry @ref register_documented_commands populates.
 * @return A reference to the shared placeholder runner.
 */
JobRunner& doc_job_runner()
{
    static SystemClock clock;
    static JobRunner runner(clock, JobRunnerOptions {});
    return runner;
}

std::string arg_to_json(const ArgSpec& arg)
{
    std::string out = "{\"name\":";
    out += jsonString(arg.name);
    out += ",\"type\":";
    out += jsonString(std::string(arg_type_name(arg.type)));
    out += ",\"required\":";
    out += arg.required ? "true" : "false";
    if (arg.type == ArgType::enumeration) {
        out += ",\"enum\":[";
        bool first = true;
        for (const std::string& value : arg.enum_values) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += jsonString(value);
        }
        out += "]";
    }
    if (arg.default_value.has_value()) {
        out += ",\"default\":";
        out += *arg.default_value;
    }
    out += ",\"doc\":";
    out += jsonString(arg.doc);
    out += "}";
    return out;
}

}  // namespace

std::string_view permission_name(Permission p)
{
    switch (p) {
        case Permission::read:
            return "read";
        case Permission::write:
            return "write";
        case Permission::migrate:
            return "migrate";
        case Permission::manage_users:
            return "manage_users";
    }
    return "read";  // Unreachable for a valid enumerator.
}

std::string_view kind_name(ExecutionKind k)
{
    switch (k) {
        case ExecutionKind::sync:
            return "sync";
        case ExecutionKind::job:
            return "job";
    }
    return "sync";  // Unreachable for a valid enumerator.
}

std::string_view arg_type_name(ArgType t)
{
    switch (t) {
        case ArgType::string:
            return "string";
        case ArgType::integer:
            return "int";
        case ArgType::boolean:
            return "bool";
        case ArgType::enumeration:
            return "enum";
        case ArgType::timestamp:
            return "timestamp";
        case ArgType::string_list:
            return "list<string>";
        case ArgType::string_map:
            return "map<string,string>";
        case ArgType::object:
            return "object";
    }
    return "string";  // Unreachable for a valid enumerator.
}

std::string descriptor_to_json(const CommandDescriptor& descriptor)
{
    std::string out = "{\"name\":";
    out += jsonString(descriptor.name);
    out += ",\"summary\":";
    out += jsonString(descriptor.summary);
    out += ",\"perm\":";
    out += jsonString(std::string(permission_name(descriptor.permission)));
    out += ",\"kind\":";
    out += jsonString(std::string(kind_name(descriptor.kind)));
    out += ",\"mutating\":";
    out += descriptor.mutating ? "true" : "false";
    out += ",\"dry_run\":";
    out += descriptor.supports_dry_run ? "true" : "false";
    out += ",\"args\":[";
    bool first = true;
    for (const ArgSpec& arg : descriptor.args) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += arg_to_json(arg);
    }
    out += "]}";
    return out;
}

void register_builtin_commands(CommandRegistry& registry)
{
    CommandDescriptor descriptor;
    descriptor.name = "command.list";
    descriptor.summary = "List all commands and their argument schemas";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.handler = [&registry](CommandContext&,
                                     const ArgsView&) -> CommandResult
    {
        std::string result = "{\"commands\":[";
        bool first = true;
        for (const CommandDescriptor* entry : registry.list()) {
            if (!first) {
                result += ",";
            }
            first = false;
            result += descriptor_to_json(*entry);
        }
        result += "]}";

        CommandResult out;
        out.result_json = std::move(result);
        return out;
    };

    registry.add(std::move(descriptor));
}

void register_documented_commands(CommandRegistry& registry)
{
    // One shared source for the catalog dump tool and the drift test. Every
    // descriptor is bound to a process-lifetime placeholder (never-executed)
    // service/lister/catalog/snapshot: the generator renders metadata only and
    // never calls execute (threat model T-19). This is a separate registry
    // instance from the live one build_command_runtime constructs.
    register_builtin_commands(registry);
    register_link_commands(registry, doc_link_service());
    register_browse_commands(
        registry, doc_lister(), doc_catalog(), app::SystemInfo {});
    register_job_commands(registry, doc_job_runner());

    // Pair commands: `link.import` always, and `link.export` whenever a page
    // source is set. A trivial (never-executed) source that returns an empty
    // page is bound here so `link.export` is documented in the catalog; the
    // service is a non-owning alias of the placeholder service (metadata only).
    PairCommandDeps pair_deps;
    pair_deps.service = std::shared_ptr<app::LinkCommandService>(
        std::shared_ptr<void>(), &doc_link_service());
    pair_deps.max_inline_bytes = 65536;
    pair_deps.list_page = [](const LinkListFilter&, std::size_t,
                             std::size_t) -> app::Result<LinkPage>
    {
        app::Result<LinkPage> out;
        out.value = LinkPage {};
        return out;
    };
    register_pair_commands(registry, std::move(pair_deps));
}

}  // namespace url_shortener::commands
