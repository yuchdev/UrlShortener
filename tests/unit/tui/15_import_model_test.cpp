#define BOOST_TEST_MODULE TuiImportModelTest

#include <cstddef>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/import_model.hpp>

/**
 * @file 15_import_model_test.cpp
 * @brief Exact-bytes request and advisory-preview tests for @ref ImportModel
 * (D7, subtask 06.0/15; FTXUI-free model library).
 *
 * Type: unit (model library, links url_shortener_tui_model).
 * Scenario: to_request emits `args` in catalog order (format, data|path,
 *           on_conflict) with every string JSON-escaped, sets options.dry_run,
 *           and omits `data` for a file source; blocking_error gates empty /
 *           oversize / empty-path; preview counts lines, blank slugs and flags
 *           comma-less CSV lines.
 * Boundaries: payload exactly max_inline_bytes OK, +1 blocked; CRLF/newline
 *           escaping; trailing newline; unicode URL bytes preserved.
 * On failure first check: to_request / blocking_error / preview in
 *           src/tui/import_model.cpp.
 */

namespace tui = url_shortener::tui;
namespace commands = url_shortener::commands;
using url_shortener::jsonString;

namespace
{

/// A default-source (paste), default-format (csv) import over a generous cap.
tui::ImportModel paste_model()
{
    return tui::ImportModel(65536);
}

}  // namespace

// ---------------------------------------------------------------------------
// CSV paste: args in catalog order, newline escaped inside data.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(csv_paste_to_request_bytes)
{
    tui::ImportModel model = paste_model();
    const std::string payload =
        "https://example.com/a,a\nhttps://example.com/b,b\n";
    model.set_text(payload);

    const commands::CommandRequest req = model.to_request("tui-1");
    BOOST_CHECK_EQUAL(req.command, "link.import");
    BOOST_CHECK_EQUAL(req.id, "tui-1");
    BOOST_CHECK(!req.options.dry_run);
    const std::string expected = "{\"format\":\"csv\",\"data\":"
        + jsonString(payload) + ",\"on_conflict\":\"skip\"}";
    BOOST_CHECK_EQUAL(req.args_json, expected);
}

// ---------------------------------------------------------------------------
// NDJSON paste: embedded quotes and newlines are escaped inside data.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ndjson_paste_escapes_quotes_and_newlines)
{
    tui::ImportModel model = paste_model();
    model.set_format(tui::ImportFormat::ndjson);
    const std::string payload = "{\"full_url\":\"https://x\",\"slug\":\"a\"}\n";
    model.set_text(payload);

    const commands::CommandRequest req = model.to_request("tui-2");
    const std::string expected = "{\"format\":\"ndjson\",\"data\":"
        + jsonString(payload) + ",\"on_conflict\":\"skip\"}";
    BOOST_CHECK_EQUAL(req.args_json, expected);
    // The raw quote/newline bytes never appear unescaped in the envelope.
    BOOST_CHECK(req.args_json.find("\n") == std::string::npos);
}

// ---------------------------------------------------------------------------
// File source: a `path` key and no `data` key.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(file_source_has_path_not_data)
{
    tui::ImportModel model = paste_model();
    model.set_source(tui::ImportSource::file);
    model.set_path("/tmp/pairs.csv");
    model.set_on_conflict(tui::OnConflict::fail);

    const commands::CommandRequest req = model.to_request("tui-3");
    BOOST_CHECK_EQUAL(
        req.args_json,
        "{\"format\":\"csv\",\"path\":\"/tmp/pairs.csv\",\"on_conflict\":"
        "\"fail\"}");
    BOOST_CHECK(req.args_json.find("\"data\"") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Dry-run reaches options.dry_run; overwrite reaches on_conflict.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dry_run_and_overwrite)
{
    tui::ImportModel model = paste_model();
    model.set_text("https://x,a\n");
    model.set_dry_run(true);
    model.set_on_conflict(tui::OnConflict::overwrite);

    const commands::CommandRequest req = model.to_request("tui-4");
    BOOST_CHECK(req.options.dry_run);
    BOOST_CHECK(req.args_json.find("\"on_conflict\":\"overwrite\"")
                != std::string::npos);
}

// ---------------------------------------------------------------------------
// Boundary: exactly max_inline_bytes is accepted, one more is blocked.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(inline_byte_boundary)
{
    tui::ImportModel model(10);
    model.set_text(std::string(10, 'a'));
    BOOST_CHECK(!model.blocking_error().has_value());

    model.set_text(std::string(11, 'a'));
    BOOST_CHECK(model.blocking_error().has_value());
}

// ---------------------------------------------------------------------------
// Blocking: empty paste, and an empty path under a file source.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(blocking_empty_cases)
{
    tui::ImportModel model = paste_model();
    BOOST_CHECK(model.blocking_error().has_value());  // empty paste

    model.set_source(tui::ImportSource::file);
    BOOST_CHECK(model.blocking_error().has_value());  // empty path
    model.set_path("/tmp/x.csv");
    BOOST_CHECK(!model.blocking_error().has_value());
}

// ---------------------------------------------------------------------------
// CRLF line endings are preserved verbatim (escaped) in the data payload.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(crlf_preserved)
{
    tui::ImportModel model = paste_model();
    const std::string payload = "https://x,a\r\nhttps://y,b\r\n";
    model.set_text(payload);
    const commands::CommandRequest req = model.to_request("tui-5");
    const std::string expected = "{\"format\":\"csv\",\"data\":"
        + jsonString(payload) + ",\"on_conflict\":\"skip\"}";
    BOOST_CHECK_EQUAL(req.args_json, expected);
}

// ---------------------------------------------------------------------------
// Preview: line count, blank-slug count, comma-less warning, trailing newline.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(preview_counts)
{
    tui::ImportModel model = paste_model();
    model.set_text(
        "https://example.com/a,a\n"
        "https://example.com/c,\n"  // blank slug
        "https://example.com/no-comma\n");  // flagged
    const tui::ImportModel::Preview p = model.preview();
    BOOST_CHECK_EQUAL(p.lines, 3u);
    BOOST_CHECK_EQUAL(p.blank_slugs, 1u);
    BOOST_CHECK_EQUAL(p.warnings.size(), 1u);
}

// ---------------------------------------------------------------------------
// Preview: a trailing newline does not add an empty content line.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(preview_trailing_newline)
{
    tui::ImportModel model = paste_model();
    model.set_text("https://x,a\n");
    BOOST_CHECK_EQUAL(model.preview().lines, 1u);
}

// ---------------------------------------------------------------------------
// Preview is paste-only: a file source yields a zero preview.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(preview_file_source_is_zero)
{
    tui::ImportModel model = paste_model();
    model.set_text("https://x,a\n");
    model.set_source(tui::ImportSource::file);
    BOOST_CHECK_EQUAL(model.preview().lines, 0u);
}

// ---------------------------------------------------------------------------
// append_within_cap: appends while it fits, clamps a partial fit, and is a
// no-op once full (so the screen skips per-char work past the cap, CWE-407).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(append_within_cap_behaviour)
{
    std::string buf;
    BOOST_CHECK(tui::append_within_cap(buf, "abc", 10));  // fits -> changed
    BOOST_CHECK_EQUAL(buf, "abc");

    // Partial fit: only one more byte fits; returns changed and clamps.
    BOOST_CHECK(tui::append_within_cap(buf, "xyz", 4));
    BOOST_CHECK_EQUAL(buf, "abcx");

    // Already full: an O(1) no-op that reports no change (the hot path past
    // the cap - the screen then skips its preview/sync).
    BOOST_CHECK(!tui::append_within_cap(buf, "more", 4));
    BOOST_CHECK_EQUAL(buf, "abcx");

    // Never splits a multibyte code point at the cut: é is 2 bytes and does
    // not fit in the 1 remaining byte, so nothing is appended.
    std::string u = "ab";  // 2 bytes
    BOOST_CHECK(!tui::append_within_cap(u, "\xc3\xa9", 3));  // é
    BOOST_CHECK_EQUAL(u, "ab");
}

// ---------------------------------------------------------------------------
// Unicode URL bytes survive round-trip into the escaped data payload.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(unicode_url_preserved)
{
    tui::ImportModel model = paste_model();
    const std::string payload =
        "https://\xc3\xa9.example/p\xc3\xa1gina,slug\n";  // é / á
    model.set_text(payload);
    const commands::CommandRequest req = model.to_request("tui-6");
    const std::string expected = "{\"format\":\"csv\",\"data\":"
        + jsonString(payload) + ",\"on_conflict\":\"skip\"}";
    BOOST_CHECK_EQUAL(req.args_json, expected);
}
