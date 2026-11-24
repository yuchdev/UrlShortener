#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/browse_commands.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_command_codec.hpp>
#include <url_shortener/commands/pair_commands.hpp>
#include <url_shortener/commands/pair_formatter.hpp>
#include <url_shortener/commands/pair_line_parser.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/// Byte cap on the inline `data` argument's decoded length, mirroring
/// `ServerConfig::max_request_body_bytes`. Defense in depth only: the
/// authoritative inline bound is `PairCommandDeps::max_inline_bytes`, enforced
/// by @ref LineSource::from_inline; the top-level args scan already bounds the
/// whole envelope at the same 64 KiB. A larger payload belongs in a `path`
/// file.
constexpr std::size_t k_import_data_max_length = 65536;

/// Byte cap on the `path` argument, matching the path-length limit
/// @ref LineSource::from_path itself enforces.
constexpr std::size_t k_import_path_max_length = 4096;

/**
 * @brief Conflict policy requested by the import's `on_conflict` argument.
 *
 * Mirrors the catalog enum. All three values are honored by the body; the
 * factory still refuses `overwrite` for a REST caller (overwrite is not
 * offered over REST — a transport rule, distinct from the capability-based
 * `path` refusal).
 */
enum class ImportConflict
{
    ///< On a slug that already exists, count the row as `skipped` and continue.
    skip,

    ///< On a slug that already exists, stop the job `failed`/`slug_conflict`,
    ///< keeping the counts so far.
    fail,

    ///< Replace the target URL of the existing link via
    ///< `LinkCommandService::ReplaceLinkTarget` and count the row as
    ///< `overwritten` (subtask 09); refused `unsupported` by the factory only
    ///< for a REST caller.
    overwrite
};

/**
 * @brief Maps the validated `on_conflict` token to an @ref ImportConflict.
 *
 * The enum @ref ArgSpec has already restricted the value to
 * `skip`/`fail`/`overwrite`, so any unrecognized text defensively maps to the
 * default @ref ImportConflict::skip.
 *
 * @param text The validated `on_conflict` argument value.
 * @return The matching @ref ImportConflict (default `skip`).
 */
ImportConflict parse_conflict(const std::string& text)
{
    if (text == "fail") {
        return ImportConflict::fail;
    }
    if (text == "overwrite") {
        return ImportConflict::overwrite;
    }
    return ImportConflict::skip;
}

/**
 * @brief Translates a @ref PathError from the file layer into a value-free
 * @ref CommandError (plan C6).
 *
 * The PathError code drives the command code; the message is fixed, value-free
 * text (never the offending path), so the result is safe to return to any
 * caller.
 *
 * @param error The refusal returned by @ref LineSource::from_path.
 * @return A @ref CommandError with `unsupported` (capability absent),
 * `not_found` (target missing) or `invalid_field` (any other rejected path).
 */
CommandError path_error_to_command_error(const PathError& error)
{
    if (error.code == "unsupported") {
        return make_error(CommandErrorCode::unsupported, "file paths disabled");
    }
    if (error.code == "not_found") {
        return make_error(CommandErrorCode::not_found, "import file not found");
    }
    return make_error(CommandErrorCode::invalid_field, "invalid import path");
}

/**
 * @brief Counts the newline bytes in @p data, the row-count estimate declared
 * as the job total for an inline source.
 *
 * @param data The inline payload.
 * @return The number of `'\n'` bytes; `0` for a payload with no newline.
 */
std::uint64_t count_newlines(const std::string& data)
{
    std::uint64_t count = 0;
    for (const char c : data) {
        if (c == '\n') {
            ++count;
        }
    }
    return count;
}

/**
 * @brief The self-contained inputs an import @ref JobBody operates on.
 *
 * Holds only deep copies of the request's decoded inputs and a shared-ownership
 * copy of the service, so the body stays valid after the originating request
 * buffer, caller and registry are gone (threat model T-87). Held through a
 * `std::shared_ptr` so the (move-only, @ref LineSource-owning) plan can live
 * inside a copyable `std::function` job body.
 */
struct ImportPlan
{
    ///< The bounded forward-only source (inline or file); read once by the
    ///< body. Move-only; owned here.
    LineSource source;

    ///< Wire format every line is parsed as.
    PairFormat format;

    ///< Conflict policy applied to an existing slug (`skip`, `fail` or
    ///< `overwrite`; a REST caller's `overwrite` is rejected before a plan is
    ///< built).
    ImportConflict on_conflict;

    ///< `true` when the body validates each row without persisting (dry-run);
    ///< `false` for a real import.
    bool dry_run;

    ///< The link service each row is created/validated through; shared by
    ///< ownership so it outlives the request.
    std::shared_ptr<app::LinkCommandService> service;

    ///< Declared total units for progress (newline count of an inline source);
    ///< absent for a file source, whose total stays unknown (0).
    std::optional<std::uint64_t> declared_total;

    /**
     * @brief Constructs a plan, moving the source and service into place.
     *
     * @param src The opened bounded source; moved in.
     * @param fmt The wire format.
     * @param conflict The conflict policy (`skip`/`fail`).
     * @param dry `true` for a dry-run (validate-only) body.
     * @param svc The link service; shared-ownership copy moved in.
     * @param total Declared total units for an inline source, or `nullopt`.
     */
    ImportPlan(LineSource src,
               PairFormat fmt,
               ImportConflict conflict,
               bool dry,
               std::shared_ptr<app::LinkCommandService> svc,
               std::optional<std::uint64_t> total)
        : source(std::move(src))
        , format(fmt)
        , on_conflict(conflict)
        , dry_run(dry)
        , service(std::move(svc))
        , declared_total(total)
    {
    }
};

/**
 * @brief Running tallies an import accumulates and serializes into its result.
 */
struct ImportCounters
{
    ///< Non-blank data rows processed
    ///< (`created + skipped + overwritten + failed`).
    std::uint64_t total = 0;

    ///< Rows that produced a new link (or would, in dry-run).
    std::uint64_t created = 0;

    ///< Rows skipped on a slug conflict under `on_conflict=skip`.
    std::uint64_t skipped = 0;

    ///< Rows whose existing link's target was replaced under
    ///< `on_conflict=overwrite` (or would be, in dry-run). Counted separately
    ///< from `created`, never folded into it.
    std::uint64_t overwritten = 0;

    ///< Rows that failed validation/decoding (a per-row error was recorded).
    std::uint64_t failed = 0;
};

/**
 * @brief Serializes the import counters and error list into the catalog result
 * object.
 *
 * Produces `{"total":n,"created":n,"skipped":n,"overwritten":n,"failed":n,`
 * `"errors":[...],"errors_omitted":n,"dry_run":bool}`. The `overwritten` field
 * is an additive v1 field (plan C2); the wire version is unchanged. The error
 * array is bounded by
 * @ref kMaxReportedImportErrors with the overflow counted in `errors_omitted`
 * (threat model T-109), and every entry carries a line number and fixed code
 * only (plan C6).
 *
 * @param counters The row tallies so far.
 * @param errors The bounded per-line error list.
 * @param dry_run The value echoed into the `dry_run` field.
 * @return The serialized result object as JSON text.
 */
std::string build_result_json(const ImportCounters& counters,
                              const ImportErrorList& errors,
                              bool dry_run)
{
    std::ostringstream os;
    os << "{\"total\":" << counters.total << ",\"created\":" << counters.created
       << ",\"skipped\":" << counters.skipped
       << ",\"overwritten\":" << counters.overwritten
       << ",\"failed\":" << counters.failed
       << ",\"errors\":" << errors.to_json()
       << ",\"errors_omitted\":" << errors.omitted()
       << ",\"dry_run\":" << (dry_run ? "true" : "false") << '}';
    return os.str();
}

/**
 * @brief Builds the `app::CreateLinkCommand` for one parsed `pair` row.
 *
 * CSV rows copy the verbatim `full_url` and optional `slug`; NDJSON rows are
 * validated against the shared `link.create` schema and decoded through the one
 * shared decoder (contract C1). No URL/slug validation is done here.
 *
 * @param row The parsed `pair` row.
 * @param format The import format.
 * @param create_args The shared `link.create` argument schema (used for NDJSON
 * only).
 * @param[out] out Filled with the decoded command on success.
 * @return `true` when a command was built; `false` when an NDJSON row failed
 * validation or decoding (the caller records a `failed`/`invalid_field` row).
 */
bool build_row_command(const ParsedRow& row,
                       PairFormat format,
                       const std::vector<ArgSpec>& create_args,
                       app::CreateLinkCommand* out)
{
    if (format == PairFormat::csv) {
        out->target_url = row.full_url;
        out->slug = row.slug;
        return true;
    }

    ArgsView view;
    CommandError validate_error;
    if (!validate_args(create_args, row.args_json, &view, &validate_error)) {
        return false;
    }
    CommandError decode_error;
    return decode_create_link_command(view, out, &decode_error);
}

/**
 * @brief Applies `on_conflict=overwrite` to one row whose slug already exists,
 * updating @p counters (and @p errors on failure) in place.
 *
 * For a dry-run this is non-mutating (threat model T-91): the replacement
 * target was already validated by `ValidateCreateLink` (a conflict means the
 * URL passed and only the slug collided), so the candidate is counted under
 * `overwritten` without calling `ReplaceLinkTarget` — nothing is written. For a
 * real run it routes the replacement through the single destructive-overwrite
 * service method `LinkCommandService::ReplaceLinkTarget` (threat model T-100):
 * the same normalize/SSRF path as create, cache-invalidating, value-free on
 * error. A generated slug can never collide, so a real conflict always carries
 * an explicit slug; the absent-slug case is guarded defensively as `failed`.
 *
 * @param plan The import plan; only `dry_run` and `service` are read.
 * @param command The decoded create command for the conflicting row (its
 * `slug` and `target_url` drive the replacement).
 * @param line_number 1-based physical line of the row, for the error list.
 * @param[in,out] counters Tallies incremented in place (`overwritten` on
 * success, `failed` on a replace failure).
 * @param[in,out] errors Per-line error list; a fixed, value-free code is added
 * on a replace failure (plan C6).
 */
void apply_overwrite(const ImportPlan& plan,
                     const app::CreateLinkCommand& command,
                     std::size_t line_number,
                     ImportCounters& counters,
                     ImportErrorList& errors)
{
    if (plan.dry_run) {
        ++counters.overwritten;
        return;
    }

    if (!command.slug.has_value()) {
        ++counters.failed;
        errors.add(line_number, "internal");
        return;
    }

    app::ReplaceLinkTargetCommand replace;
    replace.slug = *command.slug;
    replace.target_url = command.target_url;

    const app::Result<app::LinkView> result =
        plan.service->ReplaceLinkTarget(replace);
    if (result.ok()) {
        ++counters.overwritten;
    }
    else {
        ++counters.failed;
        errors.add(line_number,
                   std::string(
                       error_info_for(from_app_error(result.error.code)).name));
    }
}

/**
 * @brief Runs an import plan to completion on the given job context.
 *
 * The one import body, shared by the real (worker-thread) and dry-run
 * (request-thread, @ref IJobContext with `cancel_requested() == false`) paths;
 * only @ref ImportPlan::dry_run and the job context differ (threat model T-91).
 * Per row it: polls cancellation once (keeping partial counts on cancel,
 * threat model T-88); reads one bounded line; enforces @ref kMaxImportRows and
 * the per-line/file caps (threat model T-97); parses the row; builds the create
 * command; and routes it through `CreateLink` (or `ValidateCreateLink` in
 * dry-run) as the single validation/SSRF/slug/conflict path (contract C1). A
 * repeated slug within one dry-run file is detected via an in-file slug set so
 * it reports `slug_conflict` exactly as a real run would. On a slug conflict
 * under `on_conflict=overwrite`, @ref apply_overwrite replaces the existing
 * link's target (counting the row `overwritten`), via `ReplaceLinkTarget` on a
 * real run or non-mutatingly in dry-run (threat model T-91/T-100).
 *
 * @param plan The self-contained import inputs; its source is consumed.
 * @param ctx The job context for cancellation and progress.
 * @return A @ref JobOutcome: `ok` with the full result on success; `cancelled`
 * with the partial result on cooperative cancel; or a `failed` outcome
 * (`internal` on a read error, `invalid_field` on too many rows,
 * `slug_conflict` on `on_conflict=fail`) whose fixed message carries no row
 * content (plan C6).
 */
JobOutcome run_import(ImportPlan& plan, IJobContext& ctx)
{
    if (plan.declared_total.has_value()) {
        ctx.set_total(*plan.declared_total);
    }

    ImportCounters counters;
    ImportErrorList errors;
    std::set<std::string> seen_slugs;  // dry-run in-file duplicate detection
    PairLineParser parser(plan.format);

    std::vector<ArgSpec> create_args;
    if (plan.format == PairFormat::ndjson) {
        create_args = link_create_arg_specs();
    }

    // One place to abort the job with a fixed, value-free failure.
    const auto fail_job = [&](CommandErrorCode code, const char* message)
    {
        JobOutcome out;
        out.ok = false;
        out.failure = make_error(code, message);
        return out;
    };

    std::string line;
    std::size_t line_number = 0;
    for (;;) {
        // Poll cancellation once per row; a dry-run's NoopJobContext never
        // cancels (threat model T-91). Partial counts are kept.
        if (ctx.cancel_requested()) {
            JobOutcome out;
            out.ok = false;
            out.cancelled = true;
            out.result_json = build_result_json(counters, errors, plan.dry_run);
            return out;
        }

        const line_status status = plan.source.next(line, kMaxImportLineBytes);
        if (status == line_status::eof) {
            break;
        }
        ++line_number;
        if (status == line_status::io_error) {
            return fail_job(CommandErrorCode::internal, "import read error");
        }

        if (status == line_status::too_long) {
            if (counters.total >= kMaxImportRows) {
                return fail_job(CommandErrorCode::invalid_field,
                                "too many rows");
            }
            ++counters.total;
            ++counters.failed;
            errors.add(line_number, "invalid_field");
            ctx.advance(1);
            continue;
        }

        const ParsedRow row = parser.parse(line, line_number);
        if (row.kind == RowKind::skipped) {
            continue;  // blank line or CSV header: not a data row
        }

        if (counters.total >= kMaxImportRows) {
            return fail_job(CommandErrorCode::invalid_field, "too many rows");
        }
        ++counters.total;

        if (row.kind == RowKind::error) {
            ++counters.failed;
            errors.add(line_number, row.error_code);
            ctx.advance(1);
            continue;
        }

        app::CreateLinkCommand command;
        if (!build_row_command(row, plan.format, create_args, &command)) {
            ++counters.failed;
            errors.add(line_number, "invalid_field");
            ctx.advance(1);
            continue;
        }

        const app::Result<app::LinkView> result = plan.dry_run
            ? plan.service->ValidateCreateLink(command)
            : plan.service->CreateLink(command);

        bool is_conflict = false;
        bool ok_created = false;
        std::string other_code;

        if (result.ok()) {
            if (plan.dry_run) {
                const std::string& chosen = result.value->slug;
                if (seen_slugs.count(chosen) != 0) {
                    is_conflict = true;  // repeated slug within this file
                }
                else {
                    seen_slugs.insert(chosen);
                    ok_created = true;
                }
            }
            else {
                ok_created = true;
            }
        }
        else if (result.error.code == app::AppErrorCode::slug_conflict) {
            is_conflict = true;
        }
        else {
            other_code = std::string(
                error_info_for(from_app_error(result.error.code)).name);
        }

        if (ok_created) {
            ++counters.created;
        }
        else if (is_conflict) {
            switch (plan.on_conflict) {
                case ImportConflict::skip:
                    ++counters.skipped;
                    break;
                case ImportConflict::fail:
                    // on_conflict=fail: stop, keeping the counts so far.
                    return fail_job(CommandErrorCode::slug_conflict,
                                    "import stopped on conflict");
                case ImportConflict::overwrite:
                    apply_overwrite(
                        plan, command, line_number, counters, errors);
                    break;
            }
        }
        else {
            ++counters.failed;
            errors.add(line_number, other_code);
        }

        ctx.advance(1);
    }

    JobOutcome out;
    out.ok = true;
    out.result_json = build_result_json(counters, errors, plan.dry_run);
    return out;
}

/**
 * @brief Builds the `link.import` argument schema (catalog 3.3 table).
 *
 * @return The ordered @ref ArgSpec list: `format` (required enum
 * `csv`/`ndjson`), `data` (inline string), `path` (file string), `on_conflict`
 * (enum `skip`/`fail`/`overwrite`, default `skip`).
 */
std::vector<ArgSpec> make_link_import_args()
{
    ArgSpec format;
    format.name = "format";
    format.type = ArgType::enumeration;
    format.required = true;
    format.enum_values = {"csv", "ndjson"};
    format.doc = "Wire format of the pair stream: csv or ndjson.";

    ArgSpec data;
    data.name = "data";
    data.type = ArgType::string;
    data.max_length = k_import_data_max_length;
    data.doc = "Inline pair payload; mutually exclusive with path.";

    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    path.max_length = k_import_path_max_length;
    path.doc = "Local file path; local-operator transports only.";

    ArgSpec on_conflict;
    on_conflict.name = "on_conflict";
    on_conflict.type = ArgType::enumeration;
    on_conflict.enum_values = {"skip", "fail", "overwrite"};
    on_conflict.default_value = "\"skip\"";
    on_conflict.doc = "Behaviour on an existing slug: skip, fail or overwrite.";

    return {std::move(format),
            std::move(data),
            std::move(path),
            std::move(on_conflict)};
}

/**
 * @brief The `link.import` job factory (caller thread): validates the request,
 * opens the source and returns a self-contained body, or a failure.
 *
 * Fails fast and queues nothing on error. Checks, in order: exactly one of
 * `data`/`path`; `on_conflict=overwrite` is refused `unsupported` for a REST
 * caller only (a transport rule — overwrite is not offered over REST); a
 * `path` is refused by the file-path capability alone (`allow_paths == false`
 * ->
 * `unsupported`, threat model T-95, independent of transport name); then opens
 * the bounded source (inline/file), mapping any @ref PathError. On success it
 * deep-copies every input into an @ref ImportPlan held via `std::shared_ptr`,
 * so the returned body references nothing request- or caller-owned (threat
 * model T-87). Dry-run and real runs build the identical plan; only `dry_run`
 * (from
 * @ref CommandContext) and the job context differ (threat model T-91).
 *
 * @param deps The captured service and path/size policy (held by the factory).
 * @param context The per-invocation context; only `dry_run` is read.
 * @param args The validated argument view.
 * @return A ready @ref JobBody, or a @ref CommandError (`invalid_field`,
 * `unsupported` or `not_found`).
 */
std::variant<JobBody, CommandError> import_factory(
    const PairCommandDeps& deps,
    const CommandContext& context,
    const ArgsView& args)
{
    const std::string format_text = args.get_string("format").value_or("");
    const PairFormat format =
        format_text == "ndjson" ? PairFormat::ndjson : PairFormat::csv;

    const std::optional<std::string> data = args.get_string("data");
    const std::optional<std::string> path = args.get_string("path");

    // Exactly one of data/path (true == true and false == false both fail).
    if (data.has_value() == path.has_value()) {
        return make_error(CommandErrorCode::invalid_field,
                          "exactly one of data or path is required");
    }

    const ImportConflict on_conflict =
        parse_conflict(args.get_string("on_conflict").value_or("skip"));
    // Overwrite is destructive and is not offered over REST: a transport rule
    // (catalog 3.3), deliberately distinct from the capability-based `path`
    // refusal above. Local-operator transports (`cli`/`tui`/`stdio`) run it.
    if (on_conflict == ImportConflict::overwrite
        && context.caller.transport == "rest")
    {
        return make_error(CommandErrorCode::unsupported,
                          "on_conflict overwrite not supported");
    }

    // T-95: the refusal of `path` is driven by the capability alone, never by a
    // transport name, so any transport that does not grant it inherits deny.
    if (path.has_value() && !deps.file_paths.allow_paths) {
        return make_error(CommandErrorCode::unsupported, "file paths disabled");
    }

    std::variant<LineSource, PathError> opened = path.has_value()
        ? LineSource::from_path(*path, deps.file_paths)
        : LineSource::from_inline(*data, deps.max_inline_bytes);
    if (std::holds_alternative<PathError>(opened)) {
        return path_error_to_command_error(std::get<PathError>(opened));
    }

    std::optional<std::uint64_t> declared_total;
    if (data.has_value()) {
        declared_total = count_newlines(*data);
    }

    auto plan =
        std::make_shared<ImportPlan>(std::get<LineSource>(std::move(opened)),
                                     format,
                                     on_conflict,
                                     context.dry_run,
                                     deps.service,
                                     declared_total);

    return JobBody {[plan](IJobContext& ctx)
                    { return run_import(*plan, ctx); }};
}

// --- link.export (subtask 10) --------------------------------------------- //

/// Byte cap on the free-text `q` filter, mirroring `link.list`
/// (`app::link_list_max_query_length`); a longer value is `invalid_field`.
constexpr std::size_t k_export_q_max_length = 256;

/// Byte cap on the exact-match `tag` filter, mirroring `link.list`.
constexpr std::size_t k_export_tag_max_length = 32;

/// Byte cap on the exact-match `campaign` filter, mirroring `link.list`.
constexpr std::size_t k_export_campaign_max_length = 128;

/**
 * @brief Translates an @ref AtomicFileSink::create_new @ref PathError into a
 * value-free @ref CommandError for `link.export` (plan C6).
 *
 * `create_new` only ever yields `"unsupported"` (capability absent) or
 * `"invalid_field"` (malformed path, outside root, missing parent, or the
 * target already exists, threat model T-99); the message is fixed, value-free
 * text, never the offending path.
 *
 * @param error The refusal returned by @ref AtomicFileSink::create_new.
 * @return A @ref CommandError with `unsupported` ("file paths disabled") or
 * `invalid_field` ("invalid export path").
 */
CommandError export_path_error_to_command_error(const PathError& error)
{
    if (error.code == "unsupported") {
        return make_error(CommandErrorCode::unsupported, "file paths disabled");
    }
    return make_error(CommandErrorCode::invalid_field, "invalid export path");
}

/**
 * @brief The self-contained inputs a `link.export` @ref JobBody operates on.
 *
 * Holds only deep copies of the request's decoded inputs, a copy of the page
 * source and - for file mode - the owned @ref AtomicFileSink, so the body stays
 * valid after the request buffer, caller and registry are gone (threat model
 * T-87). Held through a `std::shared_ptr` so the (move-only, sink-owning) plan
 * can live inside a copyable `std::function` job body.
 */
struct ExportPlan
{
    ///< The single filtered-link query path; read page by page by the body.
    LinkPageSource list_page;

    ///< The filter forwarded verbatim to @ref list_page on every page.
    LinkListFilter filter;

    ///< Wire format each link is serialized as.
    PairFormat format;

    ///< The create-new atomic file sink for file mode; absent for inline mode.
    ///< Owned here; aborted by its destructor unless the body commits it.
    std::optional<AtomicFileSink> sink;

    ///< The caller-supplied path echoed into a file-mode result; empty for
    ///< inline mode. Never used for a failure message (plan C6).
    std::string path;

    /**
     * @brief Constructs a plan, moving the sink and page source into place.
     *
     * @param src The bound page source; copied in.
     * @param flt The decoded filter; moved in.
     * @param fmt The wire format.
     * @param snk The owned file sink for file mode, or `nullopt` for inline.
     * @param out_path The caller-supplied path to echo (empty for inline).
     */
    ExportPlan(LinkPageSource src,
               LinkListFilter flt,
               PairFormat fmt,
               std::optional<AtomicFileSink> snk,
               std::string out_path)
        : list_page(std::move(src))
        , filter(std::move(flt))
        , format(fmt)
        , sink(std::move(snk))
        , path(std::move(out_path))
    {
    }
};

/**
 * @brief Serializes a `link.export` result object (catalog 3.3 result shape).
 *
 * Produces `{"rows":n,"path":<path|null>,"data":<data|null>}`: exactly one of
 * `path`/`data` is non-null on success (file vs inline), and both are null on a
 * cancelled export (no file published, no inline payload returned).
 *
 * @param rows The number of link rows written.
 * @param path The caller-supplied path to echo (file mode), or empty.
 * @param data The inline payload to escape and embed (inline mode), or empty.
 * @param have_data `true` to emit the inline `data` string; `false` to emit
 * `null` (file mode, or a cancelled export).
 * @param have_path `true` to emit the `path` string; `false` to emit `null`.
 * @return The serialized result object as JSON text.
 */
std::string build_export_result_json(std::uint64_t rows,
                                     const std::string& path,
                                     const std::string& data,
                                     bool have_data,
                                     bool have_path)
{
    std::ostringstream os;
    os << "{\"rows\":" << rows << ",\"path\":";
    os << (have_path ? jsonString(path) : std::string("null"));
    os << ",\"data\":";
    os << (have_data ? jsonString(data) : std::string("null"));
    os << '}';
    return os.str();
}

/**
 * @brief Runs an export plan to completion on the given job context.
 *
 * Pages through @ref ExportPlan::list_page (fixed `created_at desc` for stable
 * offset paging), serializing each link with @ref format_csv_row /
 * @ref format_ndjson_row and either streaming it to the file sink or appending
 * it to an inline buffer capped at @ref kMaxInlineExportBytes (threat model
 * T-109). Cancellation is polled once per row (carry-forward 10.3): a cancelled
 * export keeps its partial `rows` count and, in file mode, aborts the sink so
 * no partial file is published, then returns an outcome whose `path`/`data` are
 * both null. Offset paging may skip or repeat a row under concurrent writes
 * (documented best effort; a snapshot export needs a repository cursor, out of
 * scope).
 *
 * @param plan The self-contained export inputs; its sink is consumed on commit
 * or abort.
 * @param ctx The job context for cancellation and progress.
 * @return A @ref JobOutcome: `ok` with the file/inline result on success;
 * `cancelled` with a partial (null-payload) result on cooperative cancel; or a
 * `failed` outcome whose fixed, value-free message (plan C6,
 * carry-forward 10.2) carries no path, slug, URL or row content
 * (`invalid_field` when the inline cap is reached, `internal` on a sink
 * write/commit failure, or the list error's mapped code).
 */
JobOutcome run_export(ExportPlan& plan, IJobContext& ctx)
{
    const bool file_mode = plan.sink.has_value();

    // One place to abort the job with a fixed, value-free failure (plan C6).
    const auto fail_job = [&](CommandErrorCode code, const char* message)
    {
        JobOutcome out;
        out.ok = false;
        out.failure = make_error(code, message);
        return out;
    };

    std::string inline_buf;  // inline mode only

    if (plan.format == PairFormat::csv) {
        const std::string header = csv_header();
        if (file_mode) {
            if (!plan.sink->write(header)) {
                return fail_job(CommandErrorCode::internal,
                                "export write error");
            }
        }
        else {
            inline_buf += header;  // header is tiny; always within the cap
        }
    }

    std::uint64_t rows = 0;
    std::size_t offset = 0;
    std::size_t total = 0;
    bool total_set = false;

    for (;;) {
        const app::Result<LinkPage> page =
            plan.list_page(plan.filter, offset, kExportPageSize);
        if (!page.ok()) {
            return fail_job(from_app_error(page.error.code),
                            "export list error");
        }

        const LinkPage& value = *page.value;
        if (!total_set) {
            total = value.total;
            ctx.set_total(total);
            total_set = true;
        }
        if (value.items.empty()) {
            break;
        }

        for (const app::LinkView& link : value.items) {
            // Poll cancellation once per row; partial counts are kept and, in
            // file mode, the sink is aborted so nothing is published.
            if (ctx.cancel_requested()) {
                if (file_mode) {
                    plan.sink->abort();
                }
                JobOutcome out;
                out.ok = false;
                out.cancelled = true;
                out.result_json = build_export_result_json(
                    rows, plan.path, std::string(), false, false);
                return out;
            }

            const std::string row = plan.format == PairFormat::csv
                ? format_csv_row(link)
                : format_ndjson_row(link);

            if (file_mode) {
                if (!plan.sink->write(row)) {
                    return fail_job(CommandErrorCode::internal,
                                    "export write error");
                }
            }
            else {
                if (inline_buf.size() + row.size() > kMaxInlineExportBytes) {
                    return fail_job(CommandErrorCode::invalid_field,
                                    "result too large; use path");
                }
                inline_buf += row;
            }

            ++rows;
            ctx.advance(1);
        }

        offset += kExportPageSize;
        if (offset >= total) {
            break;
        }
    }

    if (file_mode) {
        if (!plan.sink->commit()) {
            return fail_job(CommandErrorCode::internal, "export commit error");
        }
        JobOutcome out;
        out.ok = true;
        out.result_json = build_export_result_json(
            rows, plan.path, std::string(), false, true);
        return out;
    }

    JobOutcome out;
    out.ok = true;
    out.result_json =
        build_export_result_json(rows, std::string(), inline_buf, true, false);
    return out;
}

/**
 * @brief Builds the `link.export` argument schema (catalog 3.3 table).
 *
 * The filter set mirrors `link.list` (03.0/14): `status` (enum, default
 * `active`, `all` accepted), `q`, `tag`, `campaign`. `format` selects the wire
 * format and `path` optionally directs the export to a local file. `sort`/
 * `order` are deliberately **not** exposed: the export always pages
 * `created_at desc` for stable offset paging.
 *
 * @return The ordered @ref ArgSpec list for the descriptor.
 */
std::vector<ArgSpec> make_link_export_args()
{
    ArgSpec format;
    format.name = "format";
    format.type = ArgType::enumeration;
    format.required = true;
    format.enum_values = {"csv", "ndjson"};
    format.doc = "Wire format of the exported stream: csv or ndjson.";

    ArgSpec status;
    status.name = "status";
    status.type = ArgType::enumeration;
    status.enum_values = {"all", "active", "disabled", "expired", "deleted"};
    status.default_value = "\"active\"";
    status.doc = "Lifecycle filter; defaults to active.";

    ArgSpec q;
    q.name = "q";
    q.type = ArgType::string;
    q.max_length = k_export_q_max_length;
    q.doc = "Case-insensitive substring filter on slug or target URL.";

    ArgSpec tag;
    tag.name = "tag";
    tag.type = ArgType::string;
    tag.max_length = k_export_tag_max_length;
    tag.doc = "Exact-match tag filter.";

    ArgSpec campaign;
    campaign.name = "campaign";
    campaign.type = ArgType::string;
    campaign.max_length = k_export_campaign_max_length;
    campaign.doc = "Exact-match campaign name filter.";

    ArgSpec path;
    path.name = "path";
    path.type = ArgType::string;
    path.max_length = k_import_path_max_length;
    path.doc = "Local file path; local-operator transports only.";

    return {std::move(format),
            std::move(status),
            std::move(q),
            std::move(tag),
            std::move(campaign),
            std::move(path)};
}

/**
 * @brief Reads one optional string filter into @p out, leaving it empty when
 * absent.
 *
 * @param args The validated argument view.
 * @param name The filter argument key.
 * @param[out] out Set to the argument value when present; left unchanged
 * otherwise.
 */
void read_filter_field(const ArgsView& args, const char* name, std::string& out)
{
    if (const std::optional<std::string> value = args.get_string(name)) {
        out = *value;
    }
}

/**
 * @brief The `link.export` job factory (caller thread): validates the request,
 * opens the file sink if any, and returns a self-contained body or a failure.
 *
 * Runs only after `execute` has applied @ref PermissionHook, so a read-only
 * caller with `path` has already received `not_authorized` and the factory is
 * never invoked; it performs no permission check of its own. With a `path`
 * present it refuses `unsupported` when the file-path capability is absent
 * (`deps.file_paths.allow_paths == false`, threat model T-95, independent of
 * transport name), then opens an @ref AtomicFileSink via `create_new` (create-
 * new/atomic-rename from subtask 07, T-99), mapping any @ref PathError. Without
 * a `path` it builds an inline sink. On success it deep-copies every input into
 * an @ref ExportPlan held via `std::shared_ptr`, so the returned body
 * references nothing request- or caller-owned (threat model T-87).
 *
 * @param deps The captured page source and path/size policy (held by the
 * factory); `deps.list_page` is guaranteed set (export is only registered
 * then).
 * @param args The validated argument view.
 * @return A ready @ref JobBody, or a @ref CommandError (`unsupported` or
 * `invalid_field`).
 */
std::variant<JobBody, CommandError> export_factory(const PairCommandDeps& deps,
                                                   const ArgsView& args)
{
    const std::string format_text = args.get_string("format").value_or("");
    const PairFormat format =
        format_text == "ndjson" ? PairFormat::ndjson : PairFormat::csv;

    LinkListFilter filter;
    read_filter_field(args, "status", filter.status);
    read_filter_field(args, "q", filter.q);
    read_filter_field(args, "tag", filter.tag);
    read_filter_field(args, "campaign", filter.campaign);

    const std::optional<std::string> path = args.get_string("path");

    std::optional<AtomicFileSink> sink;
    std::string path_echo;
    if (path.has_value()) {
        // T-95: the refusal of `path` is driven by the capability alone, never
        // by a transport name, so any transport that does not grant it inherits
        // deny (REST obtains this via default-deny).
        if (!deps.file_paths.allow_paths) {
            return make_error(CommandErrorCode::unsupported,
                              "file paths disabled");
        }

        std::variant<AtomicFileSink, PathError> opened =
            AtomicFileSink::create_new(*path, deps.file_paths);
        if (std::holds_alternative<PathError>(opened)) {
            return export_path_error_to_command_error(
                std::get<PathError>(opened));
        }
        sink.emplace(std::get<AtomicFileSink>(std::move(opened)));
        path_echo = *path;
    }

    auto plan = std::make_shared<ExportPlan>(deps.list_page,
                                             std::move(filter),
                                             format,
                                             std::move(sink),
                                             std::move(path_echo));

    return JobBody {[plan](IJobContext& ctx)
                    { return run_export(*plan, ctx); }};
}

}  // namespace

CommandDescriptor make_link_import_descriptor(PairCommandDeps deps)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.import";
    descriptor.summary = "Bulk-import full_url to short_url pairs.";
    descriptor.args = make_link_import_args();
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::job;
    descriptor.mutating = true;
    descriptor.supports_dry_run = true;
    descriptor.maps_to = {"POST /api/v1/links:import"};
    // Capture `deps` by value so the factory (and the bodies it builds) holds a
    // shared-ownership service copy and plain values, never a reference to this
    // function's argument (threat model T-87).
    descriptor.job_factory =
        [deps](const CommandContext& context,
               const ArgsView& args) -> std::variant<JobBody, CommandError>
    { return import_factory(deps, context, args); };
    return descriptor;
}

CommandDescriptor make_link_export_descriptor(PairCommandDeps deps)
{
    CommandDescriptor descriptor;
    descriptor.name = "link.export";
    descriptor.summary = "Export filtered links as CSV or NDJSON pairs.";
    descriptor.args = make_link_export_args();
    // Base permission is read; the hook raises it to write when a `path`
    // argument directs the export to a file (decision log D16) OR when the
    // status filter enumerates `all`/`deleted` links (threat model T-71). The
    // status rule reuses the shared `link_status_permission` helper so
    // link.export and link.list can never drift on which statuses a `User` may
    // bulk-read. `command.list` reports the base `read`.
    descriptor.permission = Permission::read;
    descriptor.permission_for = [](const ArgsView& args)
    {
        if (args.has("path")
            || link_status_permission(args) == Permission::write)
        {
            return Permission::write;
        }
        return Permission::read;
    };
    descriptor.kind = ExecutionKind::job;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"GET /api/v1/links:export"};
    // Capture `deps` by value so the factory (and the bodies it builds) holds a
    // copy of the page source and plain values, never a reference to this
    // function's argument (threat model T-87).
    descriptor.job_factory =
        [deps](const CommandContext& /*context*/,
               const ArgsView& args) -> std::variant<JobBody, CommandError>
    { return export_factory(deps, args); };
    return descriptor;
}

void register_pair_commands(CommandRegistry& registry, PairCommandDeps deps)
{
    // `link.export` is registered only when a page source is available; a
    // default-constructed `list_page` means "export unavailable" (subtask 10),
    // so the command is then absent from `command.list`.
    const bool register_export = static_cast<bool>(deps.list_page);
    if (register_export) {
        registry.add(make_link_export_descriptor(deps));
    }
    registry.add(make_link_import_descriptor(std::move(deps)));
}

}  // namespace url_shortener::commands
