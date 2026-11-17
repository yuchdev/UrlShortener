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
#include <vector>

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

#ifndef _WIN32
/**
 * @brief A verified in-root parent directory descriptor plus the final name.
 *
 * Produced by @ref open_rooted_parent. The caller takes ownership of @ref fd
 * and must `close` it (directly for a read, or by handing it to an
 * @ref AtomicFileSink that closes it on commit/abort for a write).
 */
struct rooted_dir
{
    ///< Owning descriptor of the verified parent directory; caller closes it.
    int fd;

    ///< The final path component (the file name) to open relative to @ref fd.
    std::string name;
};

/**
 * @brief Splits @p target into the path components that follow @p root.
 *
 * A component-wise (not string-prefix) match, so `<root>x/...` is not mistaken
 * for a child of `<root>`. Both paths must already be lexically normalized;
 * the comparison is purely lexical and performs no filesystem access, so it
 * never follows a symlink.
 *
 * @param target The lexically normalized candidate path.
 * @param root The lexically normalized root directory.
 * @return The components of @p target after the @p root prefix (empty when
 * @p target equals @p root), or `std::nullopt` when @p target is not under
 * @p root.
 */
std::optional<std::vector<fs::path>> components_under_root(
    const fs::path& target, const fs::path& root)
{
    auto ti = target.begin();
    const auto te = target.end();
    for (auto ri = root.begin(); ri != root.end(); ++ri, ++ti) {
        if (ti == te || *ti != *ri) {
            return std::nullopt;
        }
    }
    std::vector<fs::path> rest;
    for (; ti != te; ++ti) {
        rest.push_back(*ti);
    }
    return rest;
}

/**
 * @brief Opens the in-root parent directory of @p path by a symlink-free walk.
 *
 * Closes the intermediate-parent TOCTOU (threat model T-96): rather than
 * canonicalizing the user path (which would follow directory symlinks on
 * intermediate components) and then opening it by name, this opens the
 * operator-trusted @p root once as a directory descriptor and walks the
 * relative path from there one component at a time with
 * `O_DIRECTORY | O_NOFOLLOW`. A symlinked or non-directory intermediate
 * component fails the walk (`ELOOP`/`ENOTDIR`) and is refused value-free, so an
 * in-root writer cannot swap `root/sub` for a symlink to escape the jail
 * between validation and open. The relative path is derived lexically from the
 * *given* root and user strings (same namespace, no symlink resolution); the
 * walk then starts from the *canonical* root descriptor.
 *
 * @param path The untrusted local path to resolve. Never logged.
 * @param root The operator-configured root directory (its own path is trusted
 * and canonicalized once).
 * @return A @ref rooted_dir owning the verified parent descriptor and the final
 * component name, or a @ref PathError: `"not_found"` when the canonical root or
 * an intermediate component does not exist, else `"invalid_field"`
 * (`"path outside root"`) when @p path is not lexically under @p root or any
 * intermediate component is `.`/`..`/empty, a symlink, or not a directory.
 */
std::variant<rooted_dir, PathError> open_rooted_parent(
    const std::string& path, const std::string& root)
{
    std::error_code ec;
    const fs::path canonical_root = fs::weakly_canonical(fs::path(root), ec);
    if (ec) {
        return invalid_field("path outside root");
    }

    // Derive the relative path lexically (no symlink resolution) from the given
    // root and user strings, so both are in the same namespace; the walk below
    // then starts from the canonical root descriptor.
    const fs::path norm_root = fs::path(root).lexically_normal();
    const fs::path norm_user = fs::path(path).lexically_normal();
    auto rest = components_under_root(norm_user, norm_root);
    if (!rest || rest->empty()) {
        return invalid_field("path outside root");
    }
    for (const auto& comp : *rest) {
        const std::string s = comp.string();
        if (s.empty() || s == "." || s == "..") {
            return invalid_field("path outside root");
        }
    }

    int cur = ::open(canonical_root.c_str(),
                     O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (cur < 0) {
        return PathError {"not_found", "file not found"};
    }
    // Walk every component except the last as a non-symlink directory.
    for (std::size_t i = 0; i + 1 < rest->size(); ++i) {
        const std::string comp = (*rest)[i].string();
        const int next = ::openat(cur,
                                  comp.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW
                                      | O_CLOEXEC);
        const int open_errno = errno;
        ::close(cur);
        if (next < 0) {
            if (open_errno == ENOENT) {
                return PathError {"not_found", "file not found"};
            }
            // ELOOP (a symlinked component), ENOTDIR (a non-directory
            // component), or anything else: not a plain in-root directory path.
            return invalid_field("path outside root");
        }
        cur = next;
    }
    rooted_dir out;
    out.fd = cur;
    out.name = rest->back().string();
    return out;
}
#endif  // !_WIN32

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
        // T-96, rooted POSIX: resolve the in-root parent by a symlink-free
        // component walk from the canonical root descriptor (see
        // open_rooted_parent), then open the final component with O_NOFOLLOW
        // and enforce S_ISREG and the size cap on the opened descriptor. This
        // defeats both an intermediate-parent swap (a symlinked directory
        // component fails the walk) and a final-component symlink swap (ELOOP)
        // or special-file swap (not S_ISREG) between validation and open.
        auto opened = open_rooted_parent(path, *policy.root);
        if (auto* err = std::get_if<PathError>(&opened)) {
            return *err;
        }
        const rooted_dir rd = std::get<rooted_dir>(opened);
        const int fd = ::openat(rd.fd,
                                rd.name.c_str(),
                                O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        const int open_errno = errno;
        ::close(rd.fd);
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

#ifndef _WIN32
    if (policy.root) {
        // T-96, rooted POSIX: resolve the in-root parent by a symlink-free
        // component walk (see open_rooted_parent), then create the exclusive
        // temp with openat relative to that verified descriptor. The descriptor
        // is retained in the sink so commit's linkat/unlinkat publish bind to
        // the verified directory, not to a re-resolved path.
        auto opened = open_rooted_parent(path, *policy.root);
        if (auto* err = std::get_if<PathError>(&opened)) {
            return *err;
        }
        const rooted_dir rd = std::get<rooted_dir>(opened);

        // Create-new: the target must not already exist (AT_SYMLINK_NOFOLLOW so
        // an existing symlink also counts as present).
        struct stat tst {};
        if (::fstatat(rd.fd, rd.name.c_str(), &tst, AT_SYMLINK_NOFOLLOW) == 0) {
            ::close(rd.fd);
            return invalid_field("file exists");
        }

        const std::uint64_t rn = g_temp_counter.fetch_add(1);
        const std::string rtemp = rd.name + ".tmp-"
            + std::to_string(current_pid()) + "-" + std::to_string(rn);
        const int rfd = ::openat(rd.fd,
                                 rtemp.c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW
                                     | O_CLOEXEC,
                                 0600);
        if (rfd < 0) {
            ::close(rd.fd);
            return invalid_field("cannot create temp file");
        }

        AtomicFileSink rsink;
        rsink.m_fd = rfd;
        rsink.m_dir_fd = rd.fd;
        rsink.m_temp_path = rtemp;      // name relative to m_dir_fd
        rsink.m_target_path = rd.name;  // name relative to m_dir_fd
        return rsink;
    }
#endif

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
    m_dir_fd = other.m_dir_fd;
    other.m_dir_fd = -1;
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
        m_dir_fd = other.m_dir_fd;
        other.m_dir_fd = -1;
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
        // A failed fsync means the temp's bytes may not be durable; do not
        // publish a possibly-truncated file. abort() removes the temp (and, for
        // a rooted sink, closes the directory descriptor).
        if (::fsync(m_fd) != 0) {
            abort();
            return false;
        }
        ::close(m_fd);
        m_fd = -1;
    }

    if (m_dir_fd >= 0) {
        // Rooted: publish with linkat/unlinkat bound to the verified parent
        // descriptor, not a re-resolved path (T-96). linkat is create-new (it
        // fails with EEXIST if the target exists), so of two racing sinks
        // exactly one publishes (threat model T-99).
        if (::linkat(m_dir_fd, m_temp_path.c_str(), m_dir_fd,
                     m_target_path.c_str(), 0)
            != 0)
        {
            ::unlinkat(m_dir_fd, m_temp_path.c_str(), 0);
            ::close(m_dir_fd);
            m_dir_fd = -1;
            m_temp_path.clear();
            return false;
        }
        ::unlinkat(m_dir_fd, m_temp_path.c_str(), 0);
        ::close(m_dir_fd);
        m_dir_fd = -1;
        m_temp_path.clear();
        m_committed = true;
        return true;
    }

    // Rootless: link() is create-new: it fails with EEXIST if the target
    // exists, so of two racing sinks exactly one publishes (threat model T-99).
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
        // A rooted sink's temp name is relative to the verified directory
        // descriptor; a rootless sink's is a full path.
        if (m_dir_fd >= 0) {
            ::unlinkat(m_dir_fd, m_temp_path.c_str(), 0);
        }
        else {
            ::unlink(m_temp_path.c_str());
        }
        m_temp_path.clear();
    }
    if (m_dir_fd >= 0) {
        ::close(m_dir_fd);
        m_dir_fd = -1;
    }
#endif
}

}  // namespace url_shortener::commands
