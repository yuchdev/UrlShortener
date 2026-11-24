#include "url_shortener/tui/terminal_restore.hpp"

#include <atomic>
#include <cstddef>
#include <new>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

// POSIX signal handling and termios are guarded: on platforms without the
// POSIX surface the app relies on FTXUI + the RAII guard (documented in the
// header).
#if defined(__unix__) || defined(__APPLE__)
#define URLSHORTENER_TUI_POSIX_SIGNALS 1
#include <csignal>
#include <termios.h>
#else
#define URLSHORTENER_TUI_POSIX_SIGNALS 0
#endif

namespace url_shortener::tui
{

namespace
{

/// Process-global slot the SIGTERM handler reads to find the active restore.
/// Only one @ref ScopedSignalRestore may be installed at a time, so this holds
/// at most one live restore; cleared by that installer's destructor.
std::atomic<TerminalRestore*> g_signal_restore {nullptr};

/// Companion slot for the optional line-discipline hook (termios tcsetattr),
/// run before the escape restore. Null when no hook was supplied.
std::atomic<ITerminalModeRestore*> g_signal_mode_restore {nullptr};

/// Drives the line-discipline hook (if any) then the escape restore, in that
/// order (termios before escapes, T-123). Async-signal-safe.
void run_signal_restore()
{
    ITerminalModeRestore* mode =
        g_signal_mode_restore.load(std::memory_order_acquire);
    if (mode != nullptr)
    {
        mode->restore_mode();
    }
    TerminalRestore* restore = g_signal_restore.load(std::memory_order_acquire);
    if (restore != nullptr)
    {
        restore->restore();
    }
}

#if URLSHORTENER_TUI_POSIX_SIGNALS
/// SIGTERM handler: restore the terminal (async-signal-safe), then re-raise
/// with the default disposition so the process terminates as the signal asked.
extern "C" void tui_handle_sigterm(int sig)
{
    run_signal_restore();
    struct sigaction default_action;
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    default_action.sa_flags = 0;
    sigaction(sig, &default_action, nullptr);
    raise(sig);
}
#endif

}  // namespace

struct TtyModeRestore::Impl
{
#if URLSHORTENER_TUI_POSIX_SIGNALS
    ///< The tty descriptor to restore.
    int fd = -1;

    ///< The captured canonical/echo terminal settings.
    struct termios saved;
#endif
};

TtyModeRestore::TtyModeRestore(int fd) noexcept : m_impl_(nullptr)
{
#if URLSHORTENER_TUI_POSIX_SIGNALS
    Impl* impl = new (std::nothrow) Impl;
    if (impl != nullptr && tcgetattr(fd, &impl->saved) == 0)
    {
        impl->fd = fd;
        m_impl_ = impl;
    }
    else
    {
        delete impl;  // non-tty (e.g. a pipe in CI): leave restore a no-op
    }
#else
    (void)fd;
#endif
}

TtyModeRestore::~TtyModeRestore()
{
    delete m_impl_;
}

void TtyModeRestore::restore_mode() noexcept
{
#if URLSHORTENER_TUI_POSIX_SIGNALS
    if (m_impl_ != nullptr)
    {
        tcsetattr(m_impl_->fd, TCSANOW, &m_impl_->saved);
    }
#endif
}

FdTerminalSink::FdTerminalSink(int fd) noexcept : m_fd_(fd)
{
}

void FdTerminalSink::write(const char* data, std::size_t size) noexcept
{
#if defined(_WIN32)
    ::_write(m_fd_, data, static_cast<unsigned int>(size));
#else
    const ssize_t written = ::write(m_fd_, data, size);
    (void)written;  // best-effort: a short write on teardown is not recoverable
#endif
}

TerminalRestore::TerminalRestore(ITerminalSink& sink) noexcept
    : m_sink_(sink), m_done_(false)
{
}

void TerminalRestore::restore() noexcept
{
    // exchange returns the prior value: write only on the first call.
    if (!m_done_.exchange(true, std::memory_order_acq_rel))
    {
        // sizeof includes the terminating NUL, which must not be written.
        m_sink_.write(k_terminal_restore_sequence,
                      sizeof(k_terminal_restore_sequence) - 1);
    }
}

bool TerminalRestore::restored() const noexcept
{
    return m_done_.load(std::memory_order_acquire);
}

TerminalRestoreGuard::TerminalRestoreGuard(TerminalRestore& restore) noexcept
    : m_restore_(restore)
{
}

TerminalRestoreGuard::~TerminalRestoreGuard()
{
    m_restore_.restore();
}

void run_with_restore(TerminalRestore& restore,
                      const std::function<void()>& loop)
{
    // The guard restores on both a normal return and an exception unwinding
    // out of loop(); the exception then continues to propagate (T-123).
    TerminalRestoreGuard guard(restore);
    loop();
}

ScopedSignalRestore::ScopedSignalRestore(TerminalRestore& restore,
                                         ITerminalModeRestore* mode_restore)
    : m_restore_(restore), m_mode_restore_(mode_restore)
{
    g_signal_restore.store(&restore, std::memory_order_release);
    g_signal_mode_restore.store(mode_restore, std::memory_order_release);
#if URLSHORTENER_TUI_POSIX_SIGNALS
    struct sigaction action;
    action.sa_handler = &tui_handle_sigterm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGTERM, &action, nullptr) == 0)
    {
        m_installed_ = true;
    }
#endif
}

ScopedSignalRestore::~ScopedSignalRestore()
{
#if URLSHORTENER_TUI_POSIX_SIGNALS
    if (m_installed_)
    {
        struct sigaction default_action;
        default_action.sa_handler = SIG_DFL;
        sigemptyset(&default_action.sa_mask);
        default_action.sa_flags = 0;
        sigaction(SIGTERM, &default_action, nullptr);
    }
#endif
    g_signal_mode_restore.store(nullptr, std::memory_order_release);
    g_signal_restore.store(nullptr, std::memory_order_release);
}

bool ScopedSignalRestore::installed() const noexcept
{
    return m_installed_;
}

void ScopedSignalRestore::simulate_raise() noexcept
{
    // Drive the exact restore the OS handler would (termios then escapes),
    // minus the terminating re-raise, so a test observes the signal-path
    // restore without dying.
    run_signal_restore();
}

}  // namespace url_shortener::tui
