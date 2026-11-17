#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include <url_shortener/commands/pair_file_access.hpp>

#ifdef _WIN32
#    include <process.h>
#else
#    include <cerrno>

#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace url_shortener::commands
{

namespace
{

/// The largest accepted path string; longer inputs are rejected outright.
constexpr std::size_t k_max_path_bytes = 4096;

/// Internal fixed read buffer for the descriptor-backed stream.
constexpr std::size_t k_read_chunk = 64 * 1024;

/// Builds an `"invalid_field"` refusal with a value-free message.
PathError invalid_field(const char* message)
{
    return PathError {"invalid_field", message};
}

/**
 * @brief Validates the raw path string before any filesystem access.
 *
 * Applies rules 1 and 2: reject an empty path, one longer than
 * @ref k_max_path_bytes, or one containing a NUL or ASCII control byte; on
 * Windows also reject UNC shares, device namespaces, and reserved device names
 * (which would otherwise trigger outbound SMB or a device open).
 *
 * @param path The untrusted path to validate.
 * @return An empty optional when the string is acceptable, else an
 * `"invalid_field"` @ref PathError (value-free).
 */
std::optional<PathError> validate_path_string(const std::string& path)
{
    if (path.empty() || path.size() > k_max_path_bytes) {
        return invalid_field("invalid path");
    }
    for (const char ch : path) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte == 0x7F) {
            return invalid_field("invalid path");
        }
    }

#ifdef _WIN32
    // UNC (\\server\share or //server/share) would trigger outbound SMB
    // authentication; device namespaces (\\.\, \\?\) open raw devices.
    const bool unc = path.size() >= 2 && (path[0] == '\\' || path[0] == '/')
        && (path[1] == '\\' || path[1] == '/');
    if (unc) {
        return invalid_field("invalid path");
    }

    // Reserved DOS device names, with or without an extension, in any case.
    std::string stem = fs::path(path).stem().string();
    for (char& ch : stem) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    static const char* const k_reserved[] = {"CON", "PRN", "AUX", "NUL"};
    for (const char* name : k_reserved) {
        if (stem == name) {
            return invalid_field("invalid path");
        }
    }
    if (stem.size() == 4
        && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0)
        && stem[3] >= '1' && stem[3] <= '9')
    {
        return invalid_field("invalid path");
    }
#endif

    return std::nullopt;
}

/**
 * @brief Reports whether @p target lies within @p root, compared
 * component-wise.
 *
 * Both paths must already be canonical. A component-wise (not string-prefix)
 * compare so `<root>x/...` is not mistaken for a child of `<root>` and `..` or
 * symlink escapes (already resolved by canonicalization) are caught.
 *
 * @param target The canonical target path.
 * @param root The canonical root directory.
 * @return `true` when every component of @p root is a leading component of
 * @p target.
 */
bool is_within_root(const fs::path& target, const fs::path& root)
{
    auto ti = target.begin();
    const auto te = target.end();
    for (auto ri = root.begin(); ri != root.end(); ++ri, ++ti) {
        if (ti == te || *ti != *ri) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Enforces the capability gate (rule 6) before any filesystem access.
 *
 * @param policy The caller's file-path capability.
 * @return An `"unsupported"` @ref PathError when the capability is absent
 * (default-deny, threat model T-95), else an empty optional.
 */
std::optional<PathError> check_capability(const PathPolicy& policy)
{
    if (!policy.allow_paths) {
        return PathError {"unsupported", "file paths disabled"};
    }
    return std::nullopt;
}

#ifndef _WIN32
/**
 * @brief A `std::streambuf` that reads from an owned POSIX file descriptor.
 *
 * Lets a @ref LineSource read through a descriptor that was opened with
 * `O_NOFOLLOW` and validated by `fstat`, so a rooted read is tied to the exact
 * file that passed the post-open checks (threat model T-96) rather than being
 * re-opened by name. Owns the descriptor and closes it on destruction; reads in
 * fixed @ref k_read_chunk blocks so peak memory is bounded.
 */
class fd_streambuf final : public std::streambuf
{
public:
    /**
     * @brief Takes ownership of @p fd.
     * @param fd An open, readable descriptor; closed by the destructor.
     */
    explicit fd_streambuf(int fd)
        : m_fd(fd)
    {
    }

    fd_streambuf(const fd_streambuf&) = delete;
    fd_streambuf& operator=(const fd_streambuf&) = delete;

    ~fd_streambuf() override
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

protected:
    /// Refills the buffer from the descriptor; EOF on end or hard read error.
    int_type underflow() override
    {
        if (gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        ssize_t n = 0;
        do {
            n = ::read(m_fd, m_buffer, sizeof(m_buffer));
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            return traits_type::eof();
        }
        setg(m_buffer, m_buffer, m_buffer + n);
        return traits_type::to_int_type(*gptr());
    }

private:
    ///< The owned descriptor; `-1` is never stored (always a live fd here).
    int m_fd;

    ///< Fixed read buffer, bounding peak memory regardless of file size.
    char m_buffer[k_read_chunk];
};

/**
 * @brief An input stream that owns its @ref fd_streambuf.
 *
 * Packaged as a single owning object so a @ref LineSource can hold it through
 * `std::unique_ptr<std::istream>`.
 */
class fd_istream final : public std::istream
{
public:
    /**
     * @brief Wraps @p fd in a buffered input stream.
     * @param fd An open, readable descriptor; owned by this stream.
     */
    explicit fd_istream(int fd)
        : std::istream(nullptr)
        , m_buf(fd)
    {
        rdbuf(&m_buf);
    }

private:
    ///< The owned descriptor-backed buffer.
    fd_streambuf m_buf;
};
#endif  // !_WIN32

/// A process-wide counter making each temp file name unique within a process.
std::atomic<std::uint64_t> g_temp_counter {0};

/// The current process id, portably.
long current_pid()
{
#ifdef _WIN32
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace

line_status read_bounded_line(std::istream& in,
                              std::string& line,
                              std::size_t max_line_bytes,
                              std::uint64_t* bytes_read)
{
    line.clear();
    bool saw_byte = false;
    bool overflow = false;

    char ch = '\0';
    while (in.get(ch)) {
        saw_byte = true;
        if (bytes_read != nullptr) {
            ++*bytes_read;
        }
        if (ch == '\r' && in.peek() == '\n') {
            continue;  // CRLF: drop the CR and let the LF terminate the line
        }
        if (ch == '\n') {
            return overflow ? line_status::too_long : line_status::line;
        }
        if (!overflow) {
            if (line.size() < max_line_bytes) {
                line.push_back(ch);
            }
            else {
                overflow = true;  // stop storing; keep consuming to the newline
            }
        }
    }

    if (in.bad()) {
        line.clear();
        return line_status::io_error;
    }
    if (!saw_byte) {
        line.clear();
        return line_status::eof;  // nothing left: a clean end of stream
    }
    // Final line with no trailing newline; strip a lone trailing CR if stored.
    if (!overflow && !line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return overflow ? line_status::too_long : line_status::line;
}

std::variant<LineSource, PathError> LineSource::from_path(
    const std::string& path, const PathPolicy& policy)
{
    if (auto denied = check_capability(policy)) {
        return *denied;  // unsupported, before any filesystem access
    }
    if (auto bad = validate_path_string(path)) {
        return *bad;
    }

    std::error_code ec;
    LineSource source;

#ifndef _WIN32
    if (policy.root) {
        // T-96, rooted POSIX: open relative to the canonical in-root parent
        // with O_NOFOLLOW on the final component, then enforce S_ISREG and the
        // size cap on the opened descriptor. openat against a validated
        // directory fd ties the read to a non-symlink regular file directly
        // inside the root, defeating a symlink-swap (ELOOP) or special-file
        // swap (not S_ISREG) between validation and open.
        const fs::path target(path);
        const fs::path file_name = target.filename();
        if (file_name.empty()) {
            return invalid_field("invalid path");
        }
        fs::path parent = target.parent_path();
        if (parent.empty()) {
            parent = fs::path(".");
        }
        const fs::path canonical_parent = fs::weakly_canonical(parent, ec);
        if (ec) {
            return invalid_field("path outside root");
        }
        const fs::path canonical_root =
            fs::weakly_canonical(fs::path(*policy.root), ec);
        if (ec || !is_within_root(canonical_parent / file_name, canonical_root))
        {
            return invalid_field("path outside root");
        }

        const int dir_fd = ::open(canonical_parent.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) {
            return PathError {"not_found", "file not found"};
        }
        const int fd = ::openat(dir_fd,
                                file_name.c_str(),
                                O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        const int open_errno = errno;
        ::close(dir_fd);
        if (fd < 0) {
            if (open_errno == ENOENT) {
                return PathError {"not_found", "file not found"};
            }
            // ELOOP (a symlink on the final component) and anything else: the
            // opened name is not a plain in-root file.
            return invalid_field("path outside root");
        }
        struct stat st {};
        if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            ::close(fd);
            return invalid_field("not a regular file");
        }
        if (static_cast<std::uintmax_t>(st.st_size) > kMaxImportFileBytes) {
            ::close(fd);
            return invalid_field("file too large");
        }
        source.m_stream = std::make_unique<fd_istream>(fd);
        return source;
    }
#endif

    // Rootless POSIX, and all Windows paths (root is best-effort there):
    // resolve with weakly_canonical and require an existing regular file within
    // the optional root, then read through a buffered ifstream.
    const fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        return invalid_field("invalid path");
    }
    if (policy.root) {
        const fs::path canonical_root =
            fs::weakly_canonical(fs::path(*policy.root), ec);
        if (ec || !is_within_root(canonical, canonical_root)) {
            return invalid_field("path outside root");
        }
    }

    const fs::file_status status = fs::status(canonical, ec);
    if (ec || !fs::exists(status)) {
        return PathError {"not_found", "file not found"};
    }
    if (!fs::is_regular_file(status)) {
        return invalid_field("not a regular file");
    }

    const std::uintmax_t size = fs::file_size(canonical, ec);
    if (ec || size > kMaxImportFileBytes) {
        return invalid_field("file too large");
    }

    auto stream = std::make_unique<std::ifstream>(
        canonical, std::ios::binary | std::ios::in);
    if (!stream->is_open()) {
        return PathError {"not_found", "file not found"};
    }
    source.m_stream = std::move(stream);
    return source;
}

std::variant<LineSource, PathError> LineSource::from_inline(
    std::string data, std::size_t max_bytes)
{
    if (data.size() > max_bytes) {
        return invalid_field("inline data too large");
    }
    LineSource source;
    source.m_stream = std::make_unique<std::istringstream>(std::move(data));
    return source;
}

line_status LineSource::next(std::string& line, std::size_t max_line_bytes)
{
    if (m_bytes >= kMaxImportFileBytes) {
        line.clear();
        return line_status::eof;  // stop even if the file has since grown
    }
    return read_bounded_line(*m_stream, line, max_line_bytes, &m_bytes);
}

std::uint64_t LineSource::bytes_read() const noexcept
{
    return m_bytes;
}

std::variant<AtomicFileSink, PathError> AtomicFileSink::create_new(
    const std::string& path, const PathPolicy& policy)
{
    if (auto denied = check_capability(policy)) {
        return *denied;  // unsupported, before any filesystem access
    }
    if (auto bad = validate_path_string(path)) {
        return *bad;
    }

    const fs::path target(path);
    const fs::path file_name = target.filename();
    if (file_name.empty()) {
        return invalid_field("invalid path");
    }

    std::error_code ec;
    fs::path parent = target.parent_path();
    if (parent.empty()) {
        parent = fs::path(".");
    }
    const fs::path canonical_parent = fs::weakly_canonical(parent, ec);
    if (ec || !fs::is_directory(canonical_parent, ec)) {
        return invalid_field("parent directory does not exist");
    }

    const fs::path canonical_target = canonical_parent / file_name;

    if (policy.root) {
        const fs::path canonical_root =
            fs::weakly_canonical(fs::path(*policy.root), ec);
        if (ec || !is_within_root(canonical_target, canonical_root)) {
            return invalid_field("path outside root");
        }
    }

    // Create-new: the target must not already exist (symlink_status so an
    // existing symlink also counts as present).
    if (fs::exists(fs::symlink_status(canonical_target, ec))) {
        return invalid_field("file exists");
    }

    const std::uint64_t n = g_temp_counter.fetch_add(1);
    const std::string temp_name = file_name.string() + ".tmp-"
        + std::to_string(current_pid()) + "-" + std::to_string(n);
    const fs::path temp_path = canonical_parent / temp_name;

    AtomicFileSink sink;

#ifdef _WIN32
    auto out = std::make_unique<std::ofstream>(
        temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out->is_open()) {
        return invalid_field("cannot create temp file");
    }
    sink.m_out = std::move(out);
#else
    const int fd = ::open(temp_path.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                          0600);
    if (fd < 0) {
        return invalid_field("cannot create temp file");
    }
    sink.m_fd = fd;
#endif
    sink.m_temp_path = temp_path.string();
    sink.m_target_path = canonical_target.string();
    return sink;
}

AtomicFileSink::AtomicFileSink(AtomicFileSink&& other) noexcept
{
#ifdef _WIN32
    m_out = std::move(other.m_out);
#else
    m_fd = other.m_fd;
    other.m_fd = -1;
#endif
    m_temp_path = std::move(other.m_temp_path);
    m_target_path = std::move(other.m_target_path);
    m_committed = other.m_committed;
    other.m_temp_path.clear();
    other.m_committed = true;  // leave the moved-from sink inert
}

AtomicFileSink& AtomicFileSink::operator=(AtomicFileSink&& other) noexcept
{
    if (this != &other) {
        if (!m_committed) {
            abort();
        }
#ifdef _WIN32
        m_out = std::move(other.m_out);
#else
        m_fd = other.m_fd;
        other.m_fd = -1;
#endif
        m_temp_path = std::move(other.m_temp_path);
        m_target_path = std::move(other.m_target_path);
        m_committed = other.m_committed;
        other.m_temp_path.clear();
        other.m_committed = true;
    }
    return *this;
}

AtomicFileSink::~AtomicFileSink()
{
    if (!m_committed) {
        abort();
    }
}

bool AtomicFileSink::write(std::string_view chunk)
{
#ifdef _WIN32
    if (!m_out) {
        return false;
    }
    m_out->write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    return static_cast<bool>(*m_out);
#else
    if (m_fd < 0) {
        return false;
    }
    while (!chunk.empty()) {
        const ssize_t written = ::write(m_fd, chunk.data(), chunk.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        chunk.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
#endif
}

bool AtomicFileSink::commit()
{
    if (m_committed || m_temp_path.empty()) {
        return false;
    }

#ifdef _WIN32
    if (m_out) {
        m_out->flush();
        m_out.reset();  // close the handle before renaming
    }
    std::error_code ec;
    // Create-new guard: the target must not already exist (a racing sink may
    // have committed first). On Windows rename also fails if the target exists.
    if (fs::exists(fs::symlink_status(fs::path(m_target_path), ec))) {
        abort();
        return false;
    }
    fs::rename(fs::path(m_temp_path), fs::path(m_target_path), ec);
    if (ec) {
        abort();
        return false;
    }
    m_temp_path.clear();
    m_committed = true;
    return true;
#else
    if (m_fd >= 0) {
        ::fsync(m_fd);
        ::close(m_fd);
        m_fd = -1;
    }
    // link() is create-new: it fails with EEXIST if the target exists, so of
    // two racing sinks exactly one publishes (threat model T-99).
    if (::link(m_temp_path.c_str(), m_target_path.c_str()) != 0) {
        ::unlink(m_temp_path.c_str());
        m_temp_path.clear();
        return false;
    }
    ::unlink(m_temp_path.c_str());
    m_temp_path.clear();
    m_committed = true;
    return true;
#endif
}

void AtomicFileSink::abort() noexcept
{
#ifdef _WIN32
    if (m_out) {
        m_out.reset();
    }
    if (!m_temp_path.empty()) {
        std::error_code ec;
        fs::remove(fs::path(m_temp_path), ec);
        m_temp_path.clear();
    }
#else
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (!m_temp_path.empty()) {
        ::unlink(m_temp_path.c_str());
        m_temp_path.clear();
    }
#endif
}

}  // namespace url_shortener::commands
