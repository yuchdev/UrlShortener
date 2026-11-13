#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/catalog.hpp>
#include <url_shortener/commands/command_registry.hpp>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

namespace
{

namespace commands = url_shortener::commands;

/**
 * @brief Builds the generated command block exactly as it must appear inside
 * `docs/commands/README.md`.
 *
 * The block is @ref commands::catalog_begin_marker, a newline, the output of
 * @ref commands::render_catalog_markdown (which already ends in a newline), one
 * extra newline (the blank line that separates the content from the closing
 * marker), @ref commands::catalog_end_marker and a final newline. The registry
 * is populated by @ref commands::register_documented_commands so this tool and
 * the drift test render the identical command set. The generator only reads
 * descriptor metadata; it never calls @ref commands::CommandRegistry::execute,
 * constructs no service and opens no storage (threat model T-19).
 *
 * @return The full begin-marker-to-end-marker block as deterministic text.
 */
std::string build_generated_block()
{
    commands::CommandRegistry registry;
    commands::register_documented_commands(registry);

    std::string block;
    block += commands::catalog_begin_marker;
    block += "\n";
    block += commands::render_catalog_markdown(registry);
    block += "\n";
    block += commands::catalog_end_marker;
    block += "\n";
    return block;
}

/**
 * @brief Extracts the text the generator owns from a document, i.e. the region
 * strictly between the begin and end markers (the @ref
 * commands::render_catalog_markdown output, without either marker line).
 *
 * The input is expected to contain exactly one begin marker and one end marker,
 * the begin before the end, each on its own line. The returned region starts at
 * the first character of the line following the begin marker and ends at the
 * last newline before the end marker, with that single separator newline
 * dropped, so it equals @ref commands::render_catalog_markdown verbatim.
 *
 * @param content The full document text, already CRLF-normalized by the caller.
 * @param[out] region Set to the extracted region on success; untouched on
 * failure.
 * @return `true` when both markers are present exactly once and ordered;
 * `false` when a marker is missing, duplicated, or out of order.
 */
// Kept in sync with extract_region in tests/unit/commands/12_catalog_drift.cpp;
// the drift test guards the shared behaviour.
bool extract_region(const std::string& content, std::string& region)
{
    const std::string_view begin = commands::catalog_begin_marker;
    const std::string_view end = commands::catalog_end_marker;

    const std::size_t begin_pos = content.find(begin);
    const std::size_t end_pos = content.find(end);
    if (begin_pos == std::string::npos || end_pos == std::string::npos) {
        return false;
    }
    if (content.find(begin, begin_pos + begin.size()) != std::string::npos) {
        return false;
    }
    if (content.find(end, end_pos + end.size()) != std::string::npos) {
        return false;
    }
    if (end_pos <= begin_pos) {
        return false;
    }

    const std::size_t newline = content.find('\n', begin_pos + begin.size());
    if (newline == std::string::npos || newline >= end_pos) {
        return false;
    }
    const std::size_t region_start = newline + 1;

    // The content ends at the single newline that precedes the end marker; drop
    // that separator newline so the region equals render_catalog_markdown.
    std::size_t region_end = end_pos;
    if (region_end > region_start && content[region_end - 1] == '\n') {
        --region_end;
    }

    region = content.substr(region_start, region_end - region_start);
    return true;
}

/**
 * @brief Replaces every CRLF in @p text with a single LF.
 *
 * @param text The text to normalize.
 * @return @p text with Windows line endings converted to Unix line endings.
 */
std::string normalize_crlf(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out += text[i];
    }
    return out;
}

}  // namespace

/**
 * @brief Prints the generated command block, or verifies a document against it.
 *
 * With no arguments the tool writes @ref build_generated_block to stdout in
 * binary mode (no CRLF translation on Windows), ready to be pasted into
 * `docs/commands/README.md`. With `--check <file>` it reads @p file, extracts
 * the region between the markers, CRLF-normalizes the file side, and compares
 * it with @ref commands::render_catalog_markdown; it prints a regeneration hint
 * and exits 1 on any mismatch or missing markers.
 *
 * @param argc Argument count.
 * @param argv Argument vector; `--check <file>` selects verification mode.
 * @return 0 on success (printed, or region matched); 1 when `--check` finds a
 * mismatch, missing markers, or cannot read the file; 2 on a usage error.
 */
int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    if (argc == 1) {
        std::cout << build_generated_block();
        return 0;
    }

    const std::string_view flag = argv[1];
    if (flag != "--check" || argc != 3) {
        std::cerr << "usage: command_catalog_dump [--check <file>]\n";
        return 2;
    }

    const std::string path = argv[2];
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        std::cerr << "command_catalog_dump: cannot read " << path << "\n";
        return 1;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = normalize_crlf(buffer.str());

    std::string region;
    if (!extract_region(content, region)) {
        std::cerr << "command_catalog_dump: markers missing or malformed in "
                  << path << "\n";
        return 1;
    }

    commands::CommandRegistry registry;
    commands::register_documented_commands(registry);
    const std::string expected = commands::render_catalog_markdown(registry);

    if (region != expected) {
        std::cerr
            << "command_catalog_dump: generated block in " << path
            << " is stale. Regenerate with:\n"
            << "  cmake --build cmake-build --target "
               "command_catalog_dump\n"
            << "  ./cmake-build/command_catalog_dump  # paste between the "
               "markers\n";
        return 1;
    }

    return 0;
}
