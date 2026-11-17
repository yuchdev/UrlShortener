#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

namespace url_shortener::commands
{

/**
 * @brief Hard cap on the byte size of a `path`-sourced import file.
 *
 * A file larger than this is refused before any line is read, and reading also
 * stops at this bound in case the file grows after the size check (threat model
 * T-97). Shared by @ref LineSource and the import command (subtask 08).
 */
inline constexpr std::uint64_t kMaxImportFileBytes = 64ull * 1024 * 1024;

/**
 * @brief Upper bound on an inline-`data` export payload (subtask 08/10).
 *
 * Declared here so the export command and this file layer share one
 * authoritative constant; not consumed by @ref AtomicFileSink itself, whose
 * create-new/atomic-rename contract is size agnostic.
 */
inline constexpr std::size_t kMaxInlineExportBytes = 1024 * 1024;

/**
 * @brief A refused filesystem operation: a short code plus a value-free
 * message.
 *
 * Returned by the @ref LineSource and @ref AtomicFileSink factories instead of
 * a usable object. The message never echoes the offending path or any file
 * content back to a non-local caller (plan C6, threat model T-95): it describes
 * the rule that failed, not the input.
 */
struct PathError
{
    ///< One of `"invalid_field"` (malformed/rejected path or special file),
    ///< `"not_found"` (a read target that does not exist), or `"unsupported"`
    ///< (the caller lacks the file-path capability). No other value is used.
    std::string code;

    ///< A short, value-free explanation of the rule that failed; never contains
    ///< the path or any file content.
    std::string message;
};

/**
 * @brief Outcome of one @ref read_bounded_line / @ref LineSource::next call.
 */
enum class line_status
{
    ///< A complete logical line was produced in the out-parameter (its
    ///< terminating `\n` and any preceding `\r` stripped).
    line,

    ///< The physical line exceeded `max_line_bytes`; the out-parameter holds
    ///< the first `max_line_bytes` bytes and the remainder of the physical line
    ///< was consumed without being stored (bounded memory, threat model T-97).
    too_long,

    ///< No more input: the stream was already at end before this call produced
    ///< any byte.
    eof,

    ///< The underlying stream reported a hard read error (`std::ios::badbit`).
    io_error
};

/**
 * @brief Reads one logical line from @p in into @p line, bounded in memory.
 *
 * Shared bounded line primitive, also used by the NDJSON stdio reader (subtask
 * 13); do not fork it. Strips a terminating `\n` and a single preceding `\r`,
 * so CRLF streams read like LF streams. On @ref line_status::too_long the
 * overflow bytes of the physical line are consumed without being stored, so the
 * peak allocation stays within `max_line_bytes` plus the stream's own buffer
 * regardless of how long the physical line is.
 *
 * @param in The source stream, read one physical line forward from the current
 * position. Treated as untrusted; never logged.
 * @param line Cleared, then filled with the logical line's bytes (up to
 * `max_line_bytes`). On @ref line_status::eof and @ref line_status::io_error it
 * is left empty.
 * @param max_line_bytes The maximum number of bytes stored in @p line; the CR
 * of a CRLF terminator does not count toward it.
 * @param bytes_read When non-null, incremented by the number of raw bytes
 * consumed from @p in (including terminators), letting a caller enforce a
 * whole-file byte cap across calls.
 * @return @ref line_status::line on a complete line, @ref line_status::too_long
 * when `max_line_bytes` was exceeded, @ref line_status::eof when the stream was
 * already exhausted, or @ref line_status::io_error on a hard stream error.
 */
line_status read_bounded_line(std::istream& in,
                              std::string& line,
                              std::size_t max_line_bytes,
                              std::uint64_t* bytes_read = nullptr);

/**
 * @brief Decides whether an operation may name a local filesystem `path`.
 *
 * A plain mirror of the composition-layer `PairFileOptions` (declared by
 * subtask 20) so this header need not include composition headers.
 * **Default-deny (allowlist, threat model T-95):** a `path` is refused with
 * `"unsupported"` unless the caller explicitly carries the file-path
 * capability. Only the local-operator compositions (`cmd`, `tui`, and
 * `serve-stdio` unless
 * `--no-file-paths`) set @ref allow_paths `true`; the REST per-request runtime
 * and any future transport that omits the flag inherit deny. The refusal is
 * governed by this flag alone - never by a transport name.
 */
struct PathPolicy
{
    ///< `true` only when the caller is granted the file-path capability. When
    ///< `false` (the default), @ref LineSource::from_path and
    ///< @ref AtomicFileSink::create_new return `"unsupported"` before any
    ///< filesystem access.
    bool allow_paths = false;

    ///< When set, the canonical target must lie within this canonical root
    ///< (compared component-wise); otherwise the path is refused as
    ///< `"invalid_field"`. Absent means the local operator's full filesystem
    ///< access (decision Q6), with no `..` restriction.
    std::optional<std::string> root;
};

/**
 * @brief A bounded, forward-only reader over an import source (file or inline).
 *
 * Constructed only through @ref from_path or @ref from_inline, which apply all
 * path and capability rules and return a @ref PathError on refusal. Move-only;
 * owns the underlying stream (and, on POSIX for a rooted path, the open file
 * descriptor behind it). Reading stops at @ref kMaxImportFileBytes even if the
 * file grows after the initial size check.
 */
class LineSource
{
public:
    /**
     * @brief Opens a local file for bounded line reading, applying path rules.
     *
     * The capability is checked first: when `policy.allow_paths` is `false` the
     * call returns `"unsupported"` with no filesystem access and no path
     * echoed. Otherwise the path string is validated (non-empty, <= 4096 bytes,
     * no NUL or control bytes, and on Windows no UNC/device/reserved name),
     * resolved with `weakly_canonical`, confined to `policy.root` when set
     * (component-wise prefix, re-asserted after open on POSIX via `O_NOFOLLOW`
     * + `fstat`), and required to be an existing regular file no larger than
     * @ref kMaxImportFileBytes.
     *
     * @param path The untrusted local path to read. Never logged.
     * @param policy The file-path capability and optional root jail.
     * @return A ready @ref LineSource, or a @ref PathError whose @c code is
     * `"unsupported"` (capability absent), `"not_found"` (the target does not
     * exist), or `"invalid_field"` (malformed path, path outside root, a
     * directory/FIFO/device/socket, an over-cap size, or a post-open
     * special-file/root mismatch).
     */
    static std::variant<LineSource, PathError> from_path(
        const std::string& path, const PathPolicy& policy = {});

    /**
     * @brief Wraps an in-memory string as a bounded line source.
     *
     * No path rules and no capability apply - inline `data` is always
     * available.
     *
     * @param data The import payload, moved into the source.
     * @param max_bytes The maximum accepted payload size; the caller passes
     * `ServerConfig::max_request_body_bytes`.
     * @return A ready @ref LineSource, or a @ref PathError with code
     * `"invalid_field"` when `data.size() > max_bytes`.
     */
    static std::variant<LineSource, PathError> from_inline(
        std::string data, std::size_t max_bytes);

    /**
     * @brief Reads the next logical line, enforcing the per-line and file caps.
     *
     * Delegates to @ref read_bounded_line and additionally stops at
     * @ref kMaxImportFileBytes: once that many bytes have been consumed it
     * reports @ref line_status::eof without reading further.
     *
     * @param line Cleared then filled with the next line's bytes (see
     * @ref read_bounded_line).
     * @param max_line_bytes The per-line byte cap (callers pass
     * @ref kMaxImportLineBytes).
     * @return The same statuses as @ref read_bounded_line; @ref
     * line_status::eof once the whole-file cap is reached.
     */
    line_status next(std::string& line, std::size_t max_line_bytes);

    /**
     * @brief The number of raw bytes consumed from the source so far.
     *
     * @return The running count fed to @ref read_bounded_line, used to enforce
     * the whole-file cap.
     */
    std::uint64_t bytes_read() const noexcept;

private:
    ///< Private so the only path to a @ref LineSource is a validated factory.
    LineSource() = default;

    ///< The owned source stream: an `ifstream` (rootless or Windows), a POSIX
    ///< descriptor-backed stream (rooted, for TOCTOU-safe reads), or an
    ///< `istringstream` (inline).
    std::unique_ptr<std::istream> m_stream;

    ///< Raw bytes consumed so far; bounds reads against @ref
    ///< kMaxImportFileBytes.
    std::uint64_t m_bytes = 0;
};

/**
 * @brief A create-new, atomically committed export target.
 *
 * Writes to a sibling temp file opened exclusively, then publishes it to the
 * requested path with an atomic, create-new rename/link: it never overwrites an
 * existing file, and of two sinks racing for the same target exactly one
 * commits (threat model T-99). Move-only; the destructor aborts (deleting the
 * temp) if
 * @ref commit was not called, so a failed or abandoned export leaves no partial
 * output and no temp file behind.
 */
class AtomicFileSink
{
public:
    /**
     * @brief Prepares a create-new export at @p path, applying path rules.
     *
     * The capability is checked first (`"unsupported"` when
     * `policy.allow_paths` is `false`). Then the path string is validated as in
     * @ref LineSource::from_path, the parent directory must already exist, the
     * target must not exist (create-new), and - when `policy.root` is set - the
     * canonical parent joined with the file name must lie within the canonical
     * root. The temp file is created exclusively next to the target (same
     * volume, so @ref commit's rename is atomic); on POSIX it is opened with
     * `O_EXCL | O_NOFOLLOW` in the canonical in-root parent.
     *
     * @param path The untrusted local path to create. Never logged.
     * @param policy The file-path capability and optional root jail.
     * @return A ready @ref AtomicFileSink, or a @ref PathError with code
     * `"unsupported"` (capability absent) or `"invalid_field"` (malformed path,
     * path outside root, missing parent, the target already exists, or the temp
     * file could not be created).
     */
    static std::variant<AtomicFileSink, PathError> create_new(
        const std::string& path, const PathPolicy& policy = {});

    AtomicFileSink(const AtomicFileSink&) = delete;
    AtomicFileSink& operator=(const AtomicFileSink&) = delete;

    /**
     * @brief Move-constructs, taking ownership of @p other's temp file.
     *
     * @param other The sink to move from; left inert (no temp, not committed)
     * so its destructor does nothing.
     */
    AtomicFileSink(AtomicFileSink&& other) noexcept;

    /**
     * @brief Move-assigns, aborting any pending temp this sink already held.
     *
     * @param other The sink to move from; left inert.
     * @return `*this`.
     */
    AtomicFileSink& operator=(AtomicFileSink&& other) noexcept;

    /**
     * @brief Aborts the export unless it was committed, deleting the temp file.
     */
    ~AtomicFileSink();

    /**
     * @brief Appends @p chunk to the temp file.
     *
     * @param chunk The bytes to write; written verbatim (binary mode).
     * @return `true` on success, `false` on a hard write error (after which the
     * sink should be aborted rather than committed).
     */
    bool write(std::string_view chunk);

    /**
     * @brief Flushes, closes, and atomically publishes the temp file to the
     * path.
     *
     * Uses a create-new rename/link, so it fails rather than overwrite: if the
     * target appeared since @ref create_new (e.g. a racing sink committed
     * first), the temp is removed and `false` is returned. After a successful
     * commit the destructor does nothing.
     *
     * @return `true` when the target was created from the temp file; `false`
     * when the target already exists or the close/publish failed (the temp file
     * is removed in both failure cases).
     */
    bool commit();

    /**
     * @brief Discards the export: closes and deletes the temp file, if any.
     *
     * Idempotent and safe to call after a failed @ref write or @ref commit.
     */
    void abort() noexcept;

private:
    ///< Private so the only path to a sink is the validated factory.
    AtomicFileSink() = default;

#ifdef _WIN32
    ///< Windows temp-file handle; the POSIX descriptor is unavailable there.
    std::unique_ptr<std::ostream> m_out;
#else
    ///< POSIX descriptor of the exclusively-opened temp file; `-1` when closed.
    int m_fd = -1;
#endif

    ///< Absolute path of the temp file; empty when the sink is inert.
    std::string m_temp_path;

    ///< Absolute canonical path the temp is published to on @ref commit.
    std::string m_target_path;

    ///< `true` once @ref commit has published the file, so the destructor and
    ///< move operations leave it in place.
    bool m_committed = false;
};

}  // namespace url_shortener::commands
