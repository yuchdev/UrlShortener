#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

namespace url_shortener::tui
{

/**
 * @brief The control-sequence payload that undoes every terminal mode the TUI
 * turns on, written on every exit path (T-123/T-124).
 *
 * Statically initialised (a plain `char` array, no dynamic init) so it is safe
 * to write from an async-signal handler. In order it: disables mouse tracking
 * (private modes 1000/1002/1003/1006/1015 - mouse is an ADR 0003 non-goal,
 * T-124), disables bracketed paste (2004), shows the cursor (25h) and leaves
 * the alternate screen (1049l). The individual mode markers below let a test
 * assert each step is present without hard-coding the whole string.
 */
inline constexpr char k_terminal_restore_sequence[] =
    "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?1015l"  // mouse off
    "\x1b[?2004l"   // bracketed paste off
    "\x1b[?25h"     // show cursor
    "\x1b[?1049l";  // leave alternate screen

/// Marker for "leave the alternate screen" (asserted by the restore test).
inline constexpr char k_marker_leave_alt_screen[] = "\x1b[?1049l";

/// Marker for "show the cursor".
inline constexpr char k_marker_show_cursor[] = "\x1b[?25h";

/// Marker for "disable mouse tracking" (SET 1006 is the SGR mouse mode, T-124).
inline constexpr char k_marker_disable_mouse[] = "\x1b[?1006l";

/// Marker for "disable bracketed paste".
inline constexpr char k_marker_disable_bracketed_paste[] = "\x1b[?2004l";

/**
 * @brief Where @ref TerminalRestore writes its undo sequence.
 *
 * An abstraction so the production sink (a file descriptor, @ref FdTerminalSink)
 * and a test's recording sink are interchangeable, and so @ref write can be
 * async-signal-safe: the production implementation does a single `write(2)`.
 */
class ITerminalSink
{
public:
    /**
     * @brief Virtual destructor; sinks are held through this base.
     */
    virtual ~ITerminalSink() = default;

    /**
     * @brief Writes @p size bytes starting at @p data.
     *
     * Must be `noexcept` and, for a production sink, async-signal-safe (no
     * heap, locks or stdio) so it can run from a signal handler.
     *
     * @param data Pointer to the bytes to write; not null when @p size > 0.
     * @param size Number of bytes to write.
     */
    virtual void write(const char* data, std::size_t size) noexcept = 0;
};

/**
 * @brief An @ref ITerminalSink that writes to a file descriptor with a single
 * `write(2)` / `_write` call (async-signal-safe).
 *
 * The production sink, bound to `stdout` by @ref App. Does not own the
 * descriptor.
 */
class FdTerminalSink final : public ITerminalSink
{
public:
    /**
     * @brief Binds the sink to a file descriptor.
     *
     * @param fd The descriptor to write to (e.g. the one behind `stdout`); must
     * stay open for the sink's lifetime. Not owned.
     */
    explicit FdTerminalSink(int fd) noexcept;

    /**
     * @brief Writes @p size bytes to the descriptor, ignoring short writes.
     *
     * @param data Pointer to the bytes; not null when @p size > 0.
     * @param size Number of bytes to write.
     */
    void write(const char* data, std::size_t size) noexcept override;

private:
    ///< The destination descriptor; not owned.
    int m_fd_;
};

/**
 * @brief Writes @ref k_terminal_restore_sequence to a sink exactly once across
 * every exit path - normal quit, an exception unwinding the loop, or a signal
 * (T-123/T-124).
 *
 * Idempotent: @ref restore uses an atomic flag so the RAII guard and a signal
 * handler can both call it and the terminal is restored only once (no
 * double-restore). @ref restore is `noexcept` and async-signal-safe provided
 * the injected sink is.
 */
class TerminalRestore
{
public:
    /**
     * @brief Binds the restore to its output sink.
     *
     * @param sink Where the undo sequence is written; must outlive this object.
     * Not owned.
     */
    explicit TerminalRestore(ITerminalSink& sink) noexcept;

    /**
     * @brief Writes the undo sequence to the sink the first time it is called.
     *
     * A second and later call is a no-op, so it is safe to call from both the
     * RAII guard (on unwind) and a signal handler. Async-signal-safe.
     */
    void restore() noexcept;

    /**
     * @brief Reports whether @ref restore has already run.
     *
     * @return `true` once the undo sequence has been written.
     */
    bool restored() const noexcept;

private:
    ///< The output sink; not owned.
    ITerminalSink& m_sink_;

    ///< `false` until the first @ref restore; an atomic so the guard and a
    ///< signal handler cannot both write. Must be lock-free for signal safety.
    std::atomic<bool> m_done_;
};

static_assert(std::atomic<bool>::is_always_lock_free,
              "TerminalRestore::restore must be async-signal-safe");

/**
 * @brief RAII guard that drives @ref TerminalRestore::restore on scope exit.
 *
 * Constructed around the FTXUI loop so the terminal is restored when the scope
 * ends for any reason: a normal return or an exception propagating out of the
 * loop (the destructor runs during unwinding, then the exception continues).
 */
class TerminalRestoreGuard
{
public:
    /**
     * @brief Captures the restore to drive on destruction.
     *
     * @param restore The restore to call in the destructor; must outlive this
     * guard. Not owned.
     */
    explicit TerminalRestoreGuard(TerminalRestore& restore) noexcept;

    /**
     * @brief Calls @ref TerminalRestore::restore (idempotent).
     */
    ~TerminalRestoreGuard();

    TerminalRestoreGuard(const TerminalRestoreGuard&) = delete;
    TerminalRestoreGuard& operator=(const TerminalRestoreGuard&) = delete;
    TerminalRestoreGuard(TerminalRestoreGuard&&) = delete;
    TerminalRestoreGuard& operator=(TerminalRestoreGuard&&) = delete;

private:
    ///< The restore driven on destruction; not owned.
    TerminalRestore& m_restore_;
};

/**
 * @brief Runs @p loop wrapped in a @ref TerminalRestoreGuard.
 *
 * The exact wrapper @ref App uses around `ScreenInteractive::Loop`: on a normal
 * return the guard restores once; if @p loop throws, the guard restores during
 * unwinding and the exception is **rethrown** so the caller still sees it
 * (T-123 "unwinds through App::run"). A signal handler installed by
 * @ref ScopedSignalRestore restores independently; because the restore is
 * idempotent, the two never double-restore.
 *
 * @param restore The restore to drive on every exit path.
 * @param loop The body to run (production: the FTXUI loop); must not be empty.
 */
void run_with_restore(TerminalRestore& restore,
                      const std::function<void()>& loop);

/**
 * @brief Installs a `SIGTERM` handler that drives @ref TerminalRestore::restore
 * so a `kill` does not leave a half-raw terminal (T-123).
 *
 * FTXUI owns `SIGINT`; this covers `SIGTERM`, which it does not. On POSIX the
 * constructor installs a handler that performs the async-signal-safe restore
 * and then re-raises `SIGTERM` with the default disposition so the process
 * still terminates; the destructor removes it. On platforms without POSIX
 * signals the installer is a no-op (@ref installed is `false`) and the app
 * relies on the RAII guard; this is documented, not a silent gap.
 *
 * Only one instance may be active at a time (it uses a single process-global
 * slot); @ref App owns exactly one. Not copyable or movable.
 */
class ScopedSignalRestore
{
public:
    /**
     * @brief Installs the `SIGTERM` handler bound to @p restore (POSIX).
     *
     * @param restore The restore the handler drives; must outlive this object.
     * Not owned.
     */
    explicit ScopedSignalRestore(TerminalRestore& restore);

    /**
     * @brief Removes the handler and clears the process-global slot.
     */
    ~ScopedSignalRestore();

    ScopedSignalRestore(const ScopedSignalRestore&) = delete;
    ScopedSignalRestore& operator=(const ScopedSignalRestore&) = delete;
    ScopedSignalRestore(ScopedSignalRestore&&) = delete;
    ScopedSignalRestore& operator=(ScopedSignalRestore&&) = delete;

    /**
     * @brief Reports whether a real OS handler was installed.
     *
     * @return `true` on POSIX where the `SIGTERM` handler is active; `false`
     * where signal installation is unsupported.
     */
    bool installed() const noexcept;

    /**
     * @brief Runs the restore the handler would perform, without terminating.
     *
     * Lets a test exercise the exact signal-path restore (the same bound
     * @ref TerminalRestore the OS handler would call) deterministically, with
     * no real signal and no risk of killing the test process. Safe to call when
     * no OS handler is installed.
     */
    void simulate_raise() noexcept;

private:
    ///< The restore this installer bound to the global slot; not owned.
    TerminalRestore& m_restore_;

    ///< `true` when a real OS handler was installed (POSIX).
    bool m_installed_ = false;
};

}  // namespace url_shortener::tui
