# 07 - Pair file access (import read, export write) and path rules

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/06 (limits constants)
**Blocks:** 08, 10, 13
**Estimated size:** S
**Suggested agent:** cpp-expert (security review: security-auditor)

## Objective

Centralize every filesystem interaction of `link.import` / `link.export` in two small, testable classes: a bounded line source for reading a local `path` (or an inline `data` string) and a create-new atomic file sink for export. Path arguments are untrusted input: the rules below decide what is accepted, with the explicit trust model that CLI/TUI/stdio callers are the local operator (decision Q6) and REST callers may never use `path`.

## Dependencies

- 05.0/06 constants (`kMaxImportLineBytes`).
- `<filesystem>` (C++17). Windows caveats apply (UNC and device paths).
- `ServerConfig::max_request_body_bytes` (verified, default 65536) caps inline `data`.
- Transport identity from `CallerContext` (01.0/06, field name UNVERIFIED; this subtask only needs a boolean `is_remote`/`transport == "rest"` supplied by the caller of these classes).
- `PairFileOptions {allow_paths, root}` (declared by 05.0/20 in `composition/command_runtime.hpp`, mirrored here as a plain struct
  `commands::PathPolicy` so this header does not include composition headers); filled from `serve-stdio --path-root` / `--no-file-paths`
  (05.0/16) and defaulting to "paths allowed, no root" for `cmd` and `tui`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/pair_file_access.hpp` | create | `LineSource`, `AtomicFileSink`, limits, `PathError` |
| `src/commands/pair_file_access.cpp` | create | implementation |
| `tests/unit/commands/56_pair_file_access.cpp` | create | cases below |
| `sources.cmake` | modify | add the `.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

`namespace url_shortener::commands` (NEW).

```cpp
inline constexpr std::uint64_t kMaxImportFileBytes = 64ull * 1024 * 1024;
inline constexpr std::size_t   kMaxInlineExportBytes = 1024 * 1024;

struct PathError { std::string code; std::string message; };   // code in {"invalid_field","not_found","unsupported"}

enum class line_status { line, too_long, eof, io_error };
// Shared bounded line primitive (also used by the NDJSON stdio reader, subtask 13).
// Strips "\n" and a preceding "\r". On too_long it consumes the rest of the physical
// line WITHOUT storing it, so memory stays below max_line_bytes + one buffer.
line_status read_bounded_line(std::istream& in, std::string& line, std::size_t max_line_bytes,
                              std::uint64_t* bytes_read = nullptr);

class LineSource {                                   // reads lines, bounded
public:
    static std::variant<LineSource, PathError> from_path(const std::string& path, const PathPolicy& policy = {});
    static std::variant<LineSource, PathError> from_inline(std::string data, std::size_t max_bytes);
    line_status next(std::string& line, std::size_t max_line_bytes);   // delegates to read_bounded_line
    std::uint64_t bytes_read() const noexcept;
private:
    std::unique_ptr<std::istream> m_stream; std::uint64_t m_bytes = 0;
};

class AtomicFileSink {                               // export target
public:
    static std::variant<AtomicFileSink, PathError> create_new(const std::string& path, const PathPolicy& policy = {});
    bool write(std::string_view chunk);              // false on I/O error
    bool commit();                                   // flush, close, rename temp -> path
    void abort() noexcept;                           // delete temp (destructor aborts if not committed)
};
```

Path rules (`from_path`, `create_new`), applied before opening:

1. Reject empty, longer than 4096 bytes, or containing NUL or control characters -> `invalid_field`.
2. Windows: reject UNC (`\\server\share`), device namespaces (`\\.\`, `\\?\`), and reserved device names (`CON`, `NUL`, `AUX`, `PRN`, `COMn`, `LPTn`) -> `invalid_field` (UNC would trigger outbound SMB authentication; this is the one remote-resource vector for a local path).
3. Resolve with `std::filesystem::weakly_canonical` (error code overloads, never throws); for reads the target must be a regular file (`is_regular_file` after symlink resolution) -> else `not_found` (missing) / `invalid_field` (directory, FIFO, device, socket).
4. Reads: `file_size <= kMaxImportFileBytes` else `invalid_field`; also stop reading at the cap in case the file grows (`bytes_read` check).
5. Writes: parent directory must exist; the target must NOT exist (create-new) -> `invalid_field` ("file exists"); data is written to `<path>.tmp-<pid>-<n>` opened exclusively, then `commit` renames. There is no overwrite flag in V1 (not in the catalog).
6. `PathPolicy {bool allow_paths = true; std::optional<std::string> root;}`: `allow_paths == false` -> `unsupported` ("file paths disabled")
   before any filesystem call. With `root` set, the `weakly_canonical` result (rule 3; for writes, the canonical parent joined with the
   file name) must have the canonical `root` as a path prefix compared component-wise (not as a string prefix), else `invalid_field`
   ("path outside root"); this also catches `..` and symlink escapes because both sides are canonicalized. Without a root there is no
   `..` restriction: the local operator can already read and write those files (decision Q6; `serve-stdio` for a webview parent should set
   `--path-root` or `--no-file-paths`).
7. Messages never echo the full path back to non-local callers; local callers get the path as given (it is their input).

`from_inline`: `data.size() > max_bytes` -> `invalid_field`; the caller passes `ServerConfig::max_request_body_bytes`.

## Implementation steps

1. Implement validation helpers in an anonymous namespace; one function per rule so tests can hit each.
2. Implement `read_bounded_line` with `std::istream::get`/`rdbuf` chunked reads and a fixed buffer; when `max_line_bytes` is exceeded return `too_long` and keep consuming until `\n` without storing (bounded memory). `LineSource::next` delegates to it. Subtask 13 builds on this function; do not fork it.
3. `AtomicFileSink::create_new`: build temp name next to the target (same volume so `rename` is atomic), open with `std::ofstream(std::ios::binary)` after checking absence; on `commit` use `std::filesystem::rename` (on Windows rename fails if the target exists, which doubles as the race guard).
4. Open files in binary mode so Windows does not translate `\n`.
5. Tests use a temp directory under `std::filesystem::temp_directory_path()` created per test and removed in a fixture.

## Commands

```text
cmake --build cmake-build --target commands__56_pair_file_access
ctest --test-dir cmake-build -R "^commands__56_pair_file_access$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`. Windows-only cases use `#ifdef _WIN32`; POSIX-only (FIFO, symlink) cases use `#ifndef _WIN32`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/56_pair_file_access.cpp` | `commands__56_pair_file_access` | unit | read: normal file, CRLF file, last line without newline, empty file; line exactly `max` OK, `max+1` -> `too_long` and next line still readable; file of `kMaxImportFileBytes+1` (sparse or mocked via a size seam) rejected; missing file -> `not_found`; directory -> `invalid_field`; empty path, NUL in path, 4097-byte path -> `invalid_field`; inline: exactly `max_bytes` OK, `max_bytes+1` rejected; POSIX: FIFO and `/dev/zero` rejected, symlink to regular file accepted; Windows: `\\host\share\f`, `\\.\pipe\x`, `CON` rejected; write: create-new succeeds and content equals; existing target rejected and untouched; missing parent -> `invalid_field`; abort or destructor leaves no temp file; commit failure path removes temp; two sinks racing for the same target: exactly one commits; policy: `allow_paths=false` -> `unsupported` for read and write with no filesystem access; `root=<tmp>/r`: `<tmp>/r/a.csv` accepted, `<tmp>/r/../x.csv`, `<tmp>/rx/a.csv` (string-prefix trap) and (POSIX) a symlink inside the root pointing outside -> `invalid_field` |

Manual check: on Windows, run the import of a UNC path from the TUI/CLI and confirm no network access is attempted (Process Monitor), only an immediate `invalid_field`.

## Constraints

- Security: UNC/device paths rejected; size caps on both read and inline; export never overwrites; temp file removed on failure; bounded memory per line.
- C5: no `uri.txt`/`UriMapSingleton`, no sockets.
- REST must never reach this code with a `path` (rejected earlier in 08/10 with `unsupported`); this class does not know about transports.
- C++17 only (`std::filesystem` is available in the supported toolchains), no new dependency, `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__56_pair_file_access` passes on the host platform; Windows-only and POSIX-only branches compile on both.
2. A 10 MiB single-line file is rejected without allocating more than `kMaxImportLineBytes` for the line (asserted via the `too_long` status and an allocation-free check of `line.capacity()` staying below 2x the limit).
3. Security-auditor sign-off on the path rules before 08 merges.

## Out of scope / risks

- File locking, atomic rename across volumes. (The optional root jail of rule 6 is in scope; without a root, symlink escapes are irrelevant under the local-operator trust model.)
- TOCTOU between the regular-file check and open is accepted for local operators; the size cap is re-checked while reading.
- Risk: a future remote transport (IPC with remote peers) must set `is_remote` so `path` is refused; recorded in the adding-a-transport guide (subtask 19).
