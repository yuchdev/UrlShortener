#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/links_query.hpp>

namespace url_shortener::tui
{

/**
 * @file import_model.hpp
 * @brief Pure, FTXUI-free view-models for the Import / Export screen (D7,
 * subtask 06.0/15): the import form state (@ref ImportModel) and the export
 * form state (@ref ExportModel), plus the syntactic paste pre-check.
 *
 * This header belongs to `url_shortener_tui_model` (ADR 0003 rule 6): it holds
 * no FTXUI and no colour, builds every request's `args` only through the shared
 * JSON escaper (`url_shortener::jsonString`) and never tokenizes JSON itself
 * (contract C9). The screen (06.0/15 UI layer) owns the live models and calls
 * @ref ImportModel::to_request / @ref ExportModel::to_request to turn the form
 * into one `link.import` / `link.export` envelope.
 *
 * Security (threat model T-118): a file `path` is a local-operator capability
 * passed to the command as an argument only; neither model opens, reads, writes
 * or validates a file, and @ref ImportModel::preview is a syntactic,
 * advisory-only scan (the server performs all URL/SSRF validation, C1).
 */

/**
 * @brief Where the import payload comes from (the D7 "Source" radio).
 *
 * Selects which argument @ref ImportModel::to_request emits: @ref paste sends
 * the inline `data` string, @ref file sends the `path` string. The two are
 * mutually exclusive (catalog 3.3: `data` XOR `path`). Enumerator order matches
 * the D7 radio display order.
 */
enum class ImportSource
{
    ///< Inline paste: the payload is the typed/pasted text sent as `data`.
    paste,

    ///< File path: the payload is the local file named by `path` (the TUI never
    ///< opens it; the server reads it as a local-operator capability, T-118).
    file
};

/**
 * @brief The pair wire format shared by import and export (the D7 "Format"
 * radio).
 *
 * Maps one-to-one to the `format` enum argument of `link.import` /
 * `link.export` (@ref format_to_wire). Enumerator order matches the D7 radio
 * display order.
 */
enum class ImportFormat
{
    ///< `"csv"`: one `full_url,slug` record per line.
    csv,

    ///< `"ndjson"`: one JSON object per line.
    ndjson
};

/**
 * @brief What `link.import` does when a slug already exists (the D7 "On
 * conflict" radio).
 *
 * Maps one-to-one to the `on_conflict` enum argument (@ref
 * on_conflict_to_wire). The default is @ref skip; @ref overwrite is a
 * destructive mode that must be selected explicitly (never a default) and only
 * exists while open decision D2 (Task 05.0 subtask 09) keeps it in the catalog.
 * Enumerator order matches the D7 radio display order.
 */
enum class OnConflict
{
    ///< `"skip"`: leave the existing link, skip the incoming row (the default).
    skip,

    ///< `"fail"`: stop/report the row as a conflict error.
    fail,

    ///< `"overwrite"`: replace the existing link (destructive; explicit only).
    overwrite
};

/**
 * @brief Returns the `format` wire string for an @ref ImportFormat.
 *
 * @param format The format radio choice to translate.
 * @return `"csv"` for @ref ImportFormat::csv, `"ndjson"` for
 * @ref ImportFormat::ndjson.
 */
std::string format_to_wire(ImportFormat format);

/**
 * @brief Returns the `on_conflict` wire string for an @ref OnConflict.
 *
 * @param on_conflict The conflict radio choice to translate.
 * @return `"skip"`, `"fail"` or `"overwrite"`; `"skip"` for any unrecognized
 * value (defensive, never produced by well-formed code).
 */
std::string on_conflict_to_wire(OnConflict on_conflict);

/**
 * @brief The import form state (D7): source, format, conflict policy, the
 * inline payload or file path, and the dry-run flag.
 *
 * A pure value type the screen mutates through the `set_*` methods and queries
 * through @ref preview, @ref blocking_error and @ref to_request. It never
 * touches a file: @ref set_path only stores the path string, which
 * @ref to_request forwards to the command as the `path` argument (T-118).
 */
class ImportModel
{
public:
    /**
     * @brief Builds an empty import form bounded by the inline-payload cap.
     *
     * @param max_inline_bytes The largest inline `data` payload the transport
     * accepts (mirrors `--max-request-body-bytes`, default 65536); a paste
     * larger than this is reported by @ref blocking_error so the operator must
     * switch to a file path (threat model T-121). The cap is inclusive: a
     * payload of exactly @p max_inline_bytes is accepted, one byte more is not.
     */
    explicit ImportModel(std::size_t max_inline_bytes);

    /**
     * @brief Advisory, syntactic pre-check of the pasted payload (D7 preview).
     *
     * Produced from @ref text only (never a file; a file source yields a zero
     * preview). It is purely advisory - the server performs all real validation
     * (C1) - and reports: the number of non-empty content lines (@ref lines),
     * how many of them have a blank slug (@ref blank_slugs, the D7 "blank slug
     * = generated" case), and a bounded list of @ref warnings for malformed
     * lines (a CSV line with no comma). NDJSON is counted by line only (its
     * per-line shape is validated server-side).
     */
    struct Preview
    {
        ///< Count of non-empty content lines in @ref text.
        std::size_t lines = 0;

        ///< Count of content lines whose slug field is blank (CSV only); such
        ///< a row asks the server to generate the slug.
        std::size_t blank_slugs = 0;

        ///< Human, already-value-free advisories (e.g. "line 2: no comma");
        ///< bounded so a huge paste cannot produce an unbounded list.
        std::vector<std::string> warnings;
    };

    /**
     * @brief Sets the payload source (paste vs file).
     * @param source The new source radio choice.
     */
    void set_source(ImportSource source);

    /**
     * @brief Sets the pair wire format.
     * @param format The new format radio choice.
     */
    void set_format(ImportFormat format);

    /**
     * @brief Sets the conflict policy.
     * @param on_conflict The new conflict radio choice.
     */
    void set_on_conflict(OnConflict on_conflict);

    /**
     * @brief Replaces the inline paste payload.
     * @param text The new inline payload; moved in. Stored verbatim (CRLF and
     * trailing newlines preserved) and escaped only at @ref to_request.
     */
    void set_text(std::string text);

    /**
     * @brief Replaces the file path.
     * @param path The new file path; moved in. Stored verbatim and forwarded to
     * the command as the `path` argument; never opened here (T-118).
     */
    void set_path(std::string path);

    /**
     * @brief Sets the dry-run (validate-only) flag.
     * @param dry_run `true` to request `options.dry_run` on the next submit.
     */
    void set_dry_run(bool dry_run);

    /// @return The current source radio choice.
    ImportSource source() const { return m_source_; }

    /// @return The current format radio choice.
    ImportFormat format() const { return m_format_; }

    /// @return The current conflict radio choice.
    OnConflict on_conflict() const { return m_on_conflict_; }

    /// @return The inline paste payload (verbatim bytes).
    const std::string& text() const { return m_text_; }

    /// @return The file path (verbatim bytes).
    const std::string& path() const { return m_path_; }

    /// @return `true` when the dry-run flag is set.
    bool dry_run() const { return m_dry_run_; }

    /// @return The inclusive inline-payload byte cap passed to the constructor.
    std::size_t max_inline_bytes() const { return m_max_inline_bytes_; }

    /**
     * @brief Computes the advisory @ref Preview of the current paste.
     * @return A @ref Preview of @ref text; an all-zero preview when the source
     * is @ref ImportSource::file or the paste is empty.
     */
    Preview preview() const;

    /**
     * @brief Reports why the form cannot be submitted, if anything.
     *
     * A fixed, value-free message (never the payload or path bytes) for the one
     * blocking condition, checked in order:
     * - paste source with an empty payload;
     * - paste source whose payload exceeds @ref max_inline_bytes;
     * - file source with an empty path.
     *
     * @return The blocking message when the form cannot be submitted;
     * `std::nullopt` when it is ready to submit.
     */
    std::optional<std::string> blocking_error() const;

    /**
     * @brief Builds the exact `link.import` request for the current form.
     *
     * Emits `args` with members in catalog order: `format`, then `data` (paste
     * source) or `path` (file source), then `on_conflict`. Every string value
     * is escaped with `url_shortener::jsonString`, so a hostile payload cannot
     * break the envelope (contract C9). `options.dry_run` is set from
     * @ref dry_run. The caller is expected to have checked @ref blocking_error
     * first; @ref to_request itself does not re-validate (it will still emit a
     * well-formed, escaped envelope for an empty payload).
     *
     * @param request_id The correlation id to place in
     * @ref commands::CommandRequest::id, echoed back in the response.
     * @return A `v == 1` request for `link.import` with the composed `args` and
     * `options.dry_run`.
     */
    commands::CommandRequest to_request(const std::string& request_id) const;

private:
    ///< Inclusive inline-payload byte cap (T-121); set once at construction.
    std::size_t m_max_inline_bytes_;

    ///< Current source radio choice; default paste (the D7 default).
    ImportSource m_source_ = ImportSource::paste;

    ///< Current format radio choice; default CSV (the D7 default).
    ImportFormat m_format_ = ImportFormat::csv;

    ///< Current conflict radio choice; default skip (never overwrite, C6).
    OnConflict m_on_conflict_ = OnConflict::skip;

    ///< Inline paste payload (verbatim bytes).
    std::string m_text_;

    ///< File path (verbatim bytes; never opened here, T-118).
    std::string m_path_;

    ///< Dry-run (validate-only) flag.
    bool m_dry_run_ = false;
};

/**
 * @brief The export form state (D7 counterpart): format, the link filter and an
 * optional file path.
 *
 * A pure value type the screen mutates through the `set_*` methods and queries
 * through @ref to_request. Like @ref ImportModel it never opens a file: a
 * `path` is forwarded to `link.export` as an argument only (T-118), and in a
 * read-only session the server refuses a `path` export (`not_authorized`,
 * catalog 3.3) - the screen renders that field disabled rather than opening it.
 */
class ExportModel
{
public:
    /**
     * @brief Sets the pair wire format.
     * @param format The new format radio choice.
     */
    void set_format(ImportFormat format);

    /**
     * @brief Sets the link filter (status / q / tag / campaign).
     * @param filter The filter to copy into the form.
     */
    void set_filter(const LinkListFilter& filter);

    /**
     * @brief Replaces the export file path.
     * @param path The new file path; moved in. Forwarded to the command as the
     * `path` argument; never opened here (T-118). Empty means inline export.
     */
    void set_path(std::string path);

    /// @return The current format radio choice.
    ImportFormat format() const { return m_format_; }

    /// @return The current link filter.
    const LinkListFilter& filter() const { return m_filter_; }

    /// @return The export file path (verbatim bytes); empty for inline export.
    const std::string& path() const { return m_path_; }

    /**
     * @brief Builds the exact `link.export` request for the current form.
     *
     * Emits `args` with members in catalog order: `format`, `status` (always,
     * from the filter's status radio), then `q`, `tag`, `campaign` and `path`
     * only when non-empty. Every string value is escaped with
     * `url_shortener::jsonString` (contract C9). `options` are left at their
     * defaults (export does not support dry-run).
     *
     * @param request_id The correlation id to place in
     * @ref commands::CommandRequest::id, echoed back in the response.
     * @return A `v == 1` request for `link.export` with the composed `args`.
     */
    commands::CommandRequest to_request(const std::string& request_id) const;

private:
    ///< Current format radio choice; default CSV.
    ImportFormat m_format_ = ImportFormat::csv;

    ///< Current link filter (status / q / tag / campaign).
    LinkListFilter m_filter_;

    ///< Export file path (verbatim bytes; empty for inline export).
    std::string m_path_;
};

}  // namespace url_shortener::tui
