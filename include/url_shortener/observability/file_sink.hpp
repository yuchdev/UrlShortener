#pragma once
#include <memory>
#include <mutex>
#include <string>

namespace url_shortener::observability
{

/**
 * @brief Append-only file destination for structured log lines.
 *
 * Owns an open, write-only file descriptor for the lifetime of the object and
 * releases it (via `close(2)`) in the destructor (RAII). Created only through
 * @ref Open, which fails closed rather than throwing so the caller can report
 * an initialization error without exposing the target path.
 *
 * Security/hardening properties (POSIX):
 * - The file is created with mode `0600` when missing, independent of the
 *   process umask (the open flags request exactly those bits).
 * - The open refuses to follow a symlink at the final path component
 *   (`O_NOFOLLOW`), so an attacker cannot redirect the audit log through a
 *   planted symlink.
 * - After opening, the descriptor is `fstat`-checked and the open is refused
 *   (fail closed) unless the target is a regular file owned by the effective
 *   uid, with no group/other permission bits and exactly one hard link. This
 *   stops appending to, or exposing, a log file a different user pre-planted in
 *   a shared directory (the default log path lives in the operator's CWD).
 * - An existing regular file that passes those checks is opened append-only and
 *   its permissions are left untouched (no `chmod`).
 *
 * On Windows the symlink refusal and exact `0600` mode have no direct
 * equivalent; see @ref Open for the reduced-guarantee behavior.
 *
 * Thread-safety: @ref Write serializes concurrent writers with an internal
 * mutex and each call is flushed to the kernel (an unbuffered `write(2)`), so a
 * completed audit line survives a later process crash or exit.
 */
class FileSink
{
public:
    /**
     * @brief Opens @p path for append-only logging, failing closed on error.
     *
     * POSIX: opens with `O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC |
     * O_NOFOLLOW` and mode `0600`. Windows: opens with the closest available
     * flags (`_O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY | _O_NOINHERIT`,
     * share-deny-none, `_S_IREAD | _S_IWRITE`); symlink refusal and umask-exact
     * permissions are not enforced by the platform.
     *
     * @param path Filesystem path of the log file to create or append to.
     * @param error Receives a fixed, value-free diagnostic (never the path or
     * an `errno` detail) when the open fails; left unmodified on success.
     * @return An owning @ref FileSink on success; `nullptr` when the file
     * cannot be opened because the directory is missing, permissions are
     * denied, the final component is a symlink (POSIX), the opened file is not
     * a regular file owned by the effective uid with mode `& 077 == 0` and a
     * single hard link (POSIX, the pre-planted-log hardening above), or any
     * other open failure. The caller must treat `nullptr` as a hard
     * initialization failure and must not fall back to another sink.
     */
    static std::unique_ptr<FileSink> Open(const std::string& path,
                                          std::string& error);

    /**
     * @brief Closes the owned file descriptor.
     */
    ~FileSink();

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    FileSink(FileSink&&) = delete;
    FileSink& operator=(FileSink&&) = delete;

    /**
     * @brief Appends one already-formatted log line to the file.
     *
     * Thread-safe: the write is serialized with an internal mutex and issued as
     * an unbuffered syscall, so each line is flushed to the kernel before the
     * call returns. A short write is retried until all bytes are written; an
     * unrecoverable write error is swallowed (logging must never abort the
     * caller) and the line is dropped rather than redirected elsewhere.
     *
     * @param line The complete log line to append, including its trailing
     * newline.
     */
    void Write(const std::string& line);

private:
    /**
     * @brief Wraps an already-open file descriptor; use @ref Open instead.
     * @param fd A valid, owned, write-only file descriptor.
     */
    explicit FileSink(int fd);

    ///< Owned write-only file descriptor; closed by the destructor.
    int m_fd_;

    ///< Serializes concurrent @ref Write calls.
    std::mutex m_mutex_;
};

}  // namespace url_shortener::observability
