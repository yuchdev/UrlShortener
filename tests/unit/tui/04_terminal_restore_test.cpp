#define BOOST_TEST_MODULE TuiTerminalRestoreTest

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/tui/terminal_restore.hpp>

using url_shortener::tui::FdTerminalSink;
using url_shortener::tui::ITerminalSink;
using url_shortener::tui::k_marker_disable_bracketed_paste;
using url_shortener::tui::k_marker_disable_mouse;
using url_shortener::tui::k_marker_leave_alt_screen;
using url_shortener::tui::k_marker_show_cursor;
using url_shortener::tui::run_with_restore;
using url_shortener::tui::ScopedSignalRestore;
using url_shortener::tui::TerminalRestore;

namespace
{

// A spy sink standing in for the terminal: it records every byte written so the
// test can assert which restore steps ran and how many times.
class RecordingSink final : public ITerminalSink
{
public:
    void write(const char* data, std::size_t size) noexcept override
    {
        m_bytes_.append(data, size);
    }

    const std::string& bytes() const { return m_bytes_; }

private:
    std::string m_bytes_;
};

std::size_t count_occurrences(const std::string& haystack,
                              const std::string& needle)
{
    std::size_t count = 0;
    std::string::size_type pos = haystack.find(needle);
    while (pos != std::string::npos)
    {
        ++count;
        pos = haystack.find(needle, pos + needle.size());
    }
    return count;
}

// Asserts every restore step is present exactly once (one full restore, no
// double-restore). Covers T-123 (alt-screen/cursor) and T-124 (mouse).
void expect_restored_once(const std::string& bytes)
{
    BOOST_TEST(count_occurrences(bytes, k_marker_leave_alt_screen) == 1u);
    BOOST_TEST(count_occurrences(bytes, k_marker_show_cursor) == 1u);
    BOOST_TEST(count_occurrences(bytes, k_marker_disable_mouse) == 1u);
    BOOST_TEST(count_occurrences(bytes, k_marker_disable_bracketed_paste)
               == 1u);
}

}  // namespace

// ---------------------------------------------------------------------------
// T-123: an exception from the loop (a screen component) unwinds through
// run_with_restore; the RAII guard restores exactly once and the exception
// still propagates.
// On failure first check: TerminalRestoreGuard's destructor or run_with_restore
// swallowing the exception instead of rethrowing.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(exception_from_loop_restores_once_and_propagates)
{
    RecordingSink sink;
    TerminalRestore restore(sink);

    bool threw = false;
    try
    {
        run_with_restore(restore,
                         [] { throw std::runtime_error("screen blew up"); });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    BOOST_TEST(threw);
    BOOST_TEST(restore.restored());
    expect_restored_once(sink.bytes());
}

// ---------------------------------------------------------------------------
// A normal quit restores exactly once (no double-restore).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(normal_quit_restores_once)
{
    RecordingSink sink;
    TerminalRestore restore(sink);

    run_with_restore(restore, [] {});

    BOOST_TEST(restore.restored());
    expect_restored_once(sink.bytes());
}

// ---------------------------------------------------------------------------
// A simulated SIGTERM during the loop drives the same restore, and the later
// RAII guard does not restore a second time (idempotent).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(simulated_sigterm_restores_and_does_not_double_restore)
{
    RecordingSink sink;
    TerminalRestore restore(sink);
    ScopedSignalRestore signal_restore(restore);

    // Stand in for SIGTERM arriving mid-loop: the handler drives restore.
    signal_restore.simulate_raise();
    BOOST_TEST(restore.restored());

    // The loop then unwinds normally; the guard must not restore again.
    run_with_restore(restore, [] {});
    expect_restored_once(sink.bytes());
}

// ---------------------------------------------------------------------------
// T-124: the mouse-tracking-disable step is present in the restore output.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(mouse_tracking_is_disabled_on_restore)
{
    RecordingSink sink;
    TerminalRestore restore(sink);
    restore.restore();

    BOOST_TEST(sink.bytes().find(k_marker_disable_mouse) != std::string::npos);
}

// ---------------------------------------------------------------------------
// restore() past the first call is a no-op (the idempotency the guard and the
// signal handler both rely on).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(restore_is_idempotent)
{
    RecordingSink sink;
    TerminalRestore restore(sink);

    restore.restore();
    const std::size_t after_first = sink.bytes().size();
    restore.restore();
    restore.restore();

    BOOST_TEST(sink.bytes().size() == after_first);
    expect_restored_once(sink.bytes());
}

// ---------------------------------------------------------------------------
// FdTerminalSink writes the bytes to its descriptor (smoke: no crash, correct
// length) using a temporary file as an observable descriptor.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(fd_sink_writes_to_descriptor)
{
    std::FILE* tmp = std::tmpfile();
    BOOST_REQUIRE(tmp != nullptr);
#if defined(_WIN32)
    const int fd = ::_fileno(tmp);
#else
    const int fd = ::fileno(tmp);
#endif

    FdTerminalSink sink(fd);
    TerminalRestore restore(sink);
    restore.restore();
    std::fflush(tmp);

    const long size = std::ftell(tmp);
    BOOST_TEST(size > 0);
    std::fclose(tmp);
}
