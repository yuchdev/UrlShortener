#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

/**
 * @file golden.hpp
 * @brief Header-only golden-file assertion for TUI render/snapshot tests.
 *
 * `assert_matches_golden` compares the plain text produced by
 * `testing::render_to_text` (see `render_helpers.hpp`) with a committed golden
 * file under the directory named by the `TUI_GOLDEN_DIR` compile definition
 * (set by the `register_tui_tests` CMake macro). Setting the environment
 * variable `UPDATE_GOLDEN=1` rewrites the golden instead of asserting, so a
 * golden is regenerated with the documented "bump -> UPDATE_GOLDEN=1 -> review
 * diff" procedure rather than hand-edited.
 */

namespace url_shortener::tui::testing
{

/// Absolute path of the golden directory, from the `TUI_GOLDEN_DIR` compile
/// definition. Falls back to the current directory if the test target forgot
/// to define it (which would be a CMake wiring bug, caught by a failed open).
inline std::string golden_dir()
{
#ifdef TUI_GOLDEN_DIR
    return TUI_GOLDEN_DIR;
#else
    return ".";
#endif
}

/**
 * @brief Whether `UPDATE_GOLDEN=1` requests golden regeneration.
 * @return `true` only when the environment variable `UPDATE_GOLDEN` is exactly
 *         `"1"`; `false` when it is unset or any other value (the default).
 */
inline bool update_golden_enabled()
{
    const char* value = std::getenv("UPDATE_GOLDEN");
    return value != nullptr && std::string(value) == "1";
}

/// Split @p text into lines on `'\n'`. A trailing newline does not yield an
/// extra empty element; an empty string yields a single empty line.
inline std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }
    if (text.empty() || text.back() == '\n')
    {
        lines.push_back(std::string());
    }
    return lines;
}

/**
 * @brief Produce a human-readable, line-numbered diff of two texts.
 *
 * The result lists every line position where @p expected and @p actual differ,
 * each as a `- expected` / `+ actual` pair prefixed with its 1-based line
 * number. Missing lines on either side render as `<absent>`. The diff is empty
 * (`""`) when the two inputs are identical, so callers can treat a non-empty
 * return as "mismatch" and embed it verbatim in a failure message.
 *
 * @param expected The reference text (golden contents).
 * @param actual   The text produced by the render under test.
 * @return A readable diff, or an empty string when the inputs are equal.
 */
inline std::string
text_diff(const std::string& expected, const std::string& actual)
{
    if (expected == actual)
    {
        return std::string();
    }
    const std::vector<std::string> exp = split_lines(expected);
    const std::vector<std::string> act = split_lines(actual);
    const std::size_t count = exp.size() > act.size() ? exp.size() : act.size();

    std::ostringstream diff;
    diff << "--- expected (golden)\n+++ actual\n";
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::string e = i < exp.size() ? exp[i] : std::string("<absent>");
        const std::string a = i < act.size() ? act[i] : std::string("<absent>");
        if (e != a)
        {
            diff << "@@ line " << (i + 1) << " @@\n";
            diff << "- " << e << "\n";
            diff << "+ " << a << "\n";
        }
    }
    return diff.str();
}

/**
 * @brief Assert rendered text equals a committed golden, or regenerate it.
 *
 * In the default mode (no `UPDATE_GOLDEN`), reads the golden file at
 * `golden_dir()/golden_relative_path` and asserts @p actual equals it; on
 * mismatch the failure message embeds `text_diff` so the offending lines are
 * visible. A missing golden file fails the test (it is never created silently).
 * When `UPDATE_GOLDEN=1`, writes @p actual to that path and passes, so goldens
 * are regenerated deliberately and reviewed as a diff.
 *
 * @param actual                The rendered plain text under test.
 * @param golden_relative_path  Golden file path relative to `golden_dir()`,
 *                              e.g. `"00_smoke_20x3.txt"`.
 */
inline void assert_matches_golden(const std::string& actual,
                                  const std::string& golden_relative_path)
{
    const std::string path = golden_dir() + "/" + golden_relative_path;

    if (update_golden_enabled())
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        BOOST_REQUIRE_MESSAGE(out.is_open(),
                              "UPDATE_GOLDEN: cannot open golden for write: "
                                  << path);
        out << actual;
        BOOST_TEST_MESSAGE("UPDATE_GOLDEN: wrote " << path);
        return;
    }

    std::ifstream in(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(in.is_open(),
                          "golden file not found (run with UPDATE_GOLDEN=1 to "
                          "create it): "
                              << path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string expected = buffer.str();

    BOOST_TEST_MESSAGE("comparing against golden: " << path);
    BOOST_CHECK_MESSAGE(actual == expected, "\n" << text_diff(expected, actual));
}

}  // namespace url_shortener::tui::testing
