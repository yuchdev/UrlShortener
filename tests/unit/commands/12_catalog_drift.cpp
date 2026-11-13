#define BOOST_TEST_MODULE CommandsCatalogDriftTest
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/catalog.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"

using url_shortener::commands::ArgsView;
using url_shortener::commands::catalog_begin_marker;
using url_shortener::commands::catalog_end_marker;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::Permission;
using url_shortener::commands::register_documented_commands;
using url_shortener::commands::render_catalog_markdown;

namespace
{

/// Replaces every CRLF with a single LF (file side only), so a Windows-checked
/// out `docs/commands/README.md` compares equal to the LF-only renderer output.
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

/// Extracts the region strictly between the begin and end markers, i.e. the
/// bytes the generator owns (equal to `render_catalog_markdown`). Returns
/// `false` when either marker is missing, duplicated, or out of order; mirrors
/// the extraction in `tools/command_catalog_dump.cpp`.
bool extract_region(const std::string& content, std::string& region)
{
    const std::string_view begin = catalog_begin_marker;
    const std::string_view end = catalog_end_marker;

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

    std::size_t region_end = end_pos;
    if (region_end > region_start && content[region_end - 1] == '\n') {
        --region_end;
    }

    region = content.substr(region_start, region_end - region_start);
    return true;
}

/// A trivial, never-invoked sync handler for the negative-case fixture.
CommandResult noop_handler(CommandContext&, const ArgsView&)
{
    CommandResult result;
    result.result_json = "{}";
    return result;
}

}  // namespace

// Type: unit (documentation drift guard).
// Scenario: the committed block 3.7 of docs/commands/README.md equals the
//   current renderer output; markers are present exactly once and ordered.
// Boundaries: a CRLF-checked-out file still passes (file side is normalized);
//   a missing/duplicated marker is a parse failure; an extra descriptor makes
//   the comparison fail.
// On failure first check: did a descriptor's summary/args change without
//   regenerating block 3.7? Rebuild `command_catalog_dump`, run it, and paste
//   its output between the markers in docs/commands/README.md.

BOOST_AUTO_TEST_CASE(generated_block_matches_committed_readme)
{
#ifndef SOURCE_DIR
    BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
#else
    const std::string path =
        std::string(SOURCE_DIR) + "/docs/commands/README.md";
    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in.good(), "could not open " + path);

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = normalize_crlf(buffer.str());

    std::string region;
    BOOST_REQUIRE_MESSAGE(
        extract_region(content, region),
        "docs/commands/README.md is missing the generated catalog markers; "
        "regenerate block 3.7 with cmake --build cmake-build --target "
        "command_catalog_dump then run ./cmake-build/command_catalog_dump");

    CommandRegistry registry;
    register_documented_commands(registry);
    const std::string expected = render_catalog_markdown(registry);

    BOOST_TEST_MESSAGE(
        "If this fails, block 3.7 of docs/commands/README.md is stale: run "
        "cmake --build cmake-build --target command_catalog_dump, then "
        "./cmake-build/command_catalog_dump and paste between the markers.");
    BOOST_TEST(region == expected);
#endif
}

BOOST_AUTO_TEST_CASE(markers_present_exactly_once_and_ordered)
{
    const std::string begin(catalog_begin_marker);
    const std::string end(catalog_end_marker);

    const std::string good = begin + "\nbody\n\n" + end + "\n";
    std::string region;
    BOOST_TEST(extract_region(good, region));
    BOOST_TEST(region == "body\n");

    // Reversed order is rejected.
    const std::string reversed = end + "\nbody\n\n" + begin + "\n";
    BOOST_TEST(!extract_region(reversed, region));

    // A duplicate begin marker is rejected.
    const std::string duplicate = begin + "\nbody\n\n" + end + "\n" + begin;
    BOOST_TEST(!extract_region(duplicate, region));
}

BOOST_AUTO_TEST_CASE(missing_markers_fail_extraction)
{
    std::string region;
    BOOST_TEST(!extract_region("no markers here\n", region));

    const std::string only_begin =
        std::string(catalog_begin_marker) + "\nbody\n";
    BOOST_TEST(!extract_region(only_begin, region));
}

BOOST_AUTO_TEST_CASE(extra_descriptor_breaks_comparison)
{
    CommandRegistry documented;
    register_documented_commands(documented);
    const std::string expected = render_catalog_markdown(documented);

    CommandRegistry augmented;
    register_documented_commands(augmented);
    CommandDescriptor extra;
    extra.name = "zzz.extra";
    extra.summary = "An undocumented command not in the committed block";
    extra.permission = Permission::read;
    extra.kind = ExecutionKind::sync;
    extra.mutating = false;
    extra.handler = noop_handler;
    augmented.add(std::move(extra));

    BOOST_TEST(render_catalog_markdown(augmented) != expected);
}

// Scenario: a README checked out with CRLF line endings (e.g. git autocrlf on
// Windows) still matches the LF-only generator output after normalization.
// Builds the document in memory so the CRLF path runs on every platform
// (verify-subtask PARTIAL on 01.0/12).
BOOST_AUTO_TEST_CASE(crlf_checkout_still_matches)
{
    CommandRegistry registry;
    register_documented_commands(registry);
    const std::string expected = render_catalog_markdown(registry);

    std::string lf_document = "# Catalog\n\nIntro prose.\n\n";
    lf_document += std::string(catalog_begin_marker) + "\n";
    lf_document += expected + "\n";
    lf_document += std::string(catalog_end_marker) + "\n\nTrailing prose.\n";

    std::string crlf_document;
    for (const char ch : lf_document) {
        if (ch == '\n') {
            crlf_document += '\r';
        }
        crlf_document += ch;
    }
    BOOST_REQUIRE(crlf_document.find("\r\n") != std::string::npos);

    std::string region;
    BOOST_REQUIRE(extract_region(normalize_crlf(crlf_document), region));
    BOOST_TEST(region == expected);
}
