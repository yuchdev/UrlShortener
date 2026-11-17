#include "url_shortener/observability/file_sink.hpp"

#include <cstddef>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace url_shortener::observability
{

namespace
{

/// Fixed, value-free diagnostic for every open failure. Never includes the
/// path or an `errno` detail, because the log path can be sensitive and the
/// caller surfaces this text verbatim (threat model: path disclosure).
constexpr char k_open_failure[] = "log file unavailable";

}  // namespace

std::unique_ptr<FileSink> FileSink::Open(const std::string& path,
                                         std::string& error)
{
#if defined(_WIN32)
    // Windows has no O_NOFOLLOW and no umask-exact mode; the closest flags are
    // used and the reduced guarantees are documented on the header.
    // _O_NOINHERIT mirrors O_CLOEXEC so children do not inherit the handle.
    int fd = -1;
    const errno_t rc = _sopen_s(&fd, path.c_str(),
                                _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY
                                    | _O_NOINHERIT,
                                _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (rc != 0 || fd < 0) {
        error = k_open_failure;
        return nullptr;
    }
#else
    // O_NOFOLLOW refuses a symlink at the final component only; O_CREAT with
    // mode 0600 is umask-independent for these bits because open() ANDs the
    // mode with ~umask but the owner bits are what we request and the file is
    // private either way. O_CLOEXEC keeps the fd out of exec'd children.
    const int fd = ::open(path.c_str(),
                          O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC
                              | O_NOFOLLOW,
                          0600);
    if (fd < 0) {
        error = k_open_failure;
        return nullptr;
    }
#endif
    return std::unique_ptr<FileSink>(new FileSink(fd));
}

FileSink::FileSink(int fd)
    : m_fd_(fd)
{
}

FileSink::~FileSink()
{
    if (m_fd_ >= 0) {
#if defined(_WIN32)
        ::_close(m_fd_);
#else
        ::close(m_fd_);
#endif
        m_fd_ = -1;
    }
}

void FileSink::Write(const std::string& line)
{
    std::lock_guard<std::mutex> guard(m_mutex_);
    if (m_fd_ < 0) {
        return;
    }

    const char* data = line.data();
    std::size_t remaining = line.size();
    while (remaining > 0) {
#if defined(_WIN32)
        const int n = ::_write(m_fd_, data,
                               static_cast<unsigned int>(remaining));
#else
        const ssize_t n = ::write(m_fd_, data, remaining);
#endif
        if (n <= 0) {
            // Unrecoverable write error: drop the remainder rather than abort
            // the caller or redirect the audit line to another sink.
            return;
        }
        data += n;
        remaining -= static_cast<std::size_t>(n);
    }
}

}  // namespace url_shortener::observability
