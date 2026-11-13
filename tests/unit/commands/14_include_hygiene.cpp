#define BOOST_TEST_MODULE CommandsIncludeHygieneTest
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace
{

/// The includes a public commands header may never pull in. Keep this list in
/// sync with the task spec,
/// docs/roadmap/0006-unified-command-model-and-tui/01.0-command-model-core/
/// 13-error-code-coverage-and-include-hygiene.md and the forbidden list in
/// docs/commands/README.md. Each entry is matched as a substring of an
/// `#include` target: `boost/asio`, `boost/beast` and `boost/program_options`
/// keep transports out of the core; `ftxui` keeps the TUI out; the
/// `url_shortener/...` directory prefixes forbid reaching http/storage/
/// composition/cli/tui types directly (C1/C8/C9, threat model T-18);
/// `core/utils.hpp` is banned outright because it transitively pulls in Beast,
/// which the textual scan cannot otherwise see; `regex` bans `<regex>` (ReDoS /
/// recursion, threat model T-01).
constexpr std::array<std::string_view, 11> k_forbidden = {
    "boost/beast",
    "boost/asio",
    "boost/program_options",
    "ftxui",
    "url_shortener/http/",
    "url_shortener/storage/",
    "url_shortener/composition/",
    "url_shortener/cli/",
    "url_shortener/tui/",
    "url_shortener/core/utils.hpp",
    "regex",
};

/// Extracts the include target (the text between `<...>` or `"..."`) of one
/// source line, or an empty string when the line is not an `#include`.
std::string include_target(const std::string& line)
{
    std::size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') {
        return {};
    }
    i = line.find_first_not_of(" \t", i + 1);
    const std::string_view keyword = "include";
    if (i == std::string::npos || line.compare(i, keyword.size(), keyword) != 0)
    {
        return {};
    }
    i = line.find_first_not_of(" \t", i + keyword.size());
    if (i == std::string::npos) {
        return {};
    }
    char close = '\0';
    if (line[i] == '<') {
        close = '>';
    }
    else if (line[i] == '"') {
        close = '"';
    }
    else {
        return {};
    }
    const std::size_t end = line.find(close, i + 1);
    if (end == std::string::npos) {
        return {};
    }
    return line.substr(i + 1, end - (i + 1));
}

/// Returns the `(filename, include-target)` pairs of every `*.hpp` under @p dir
/// (recursively). The same function scans the real commands headers and the
/// negative self-test fixture, so the checker is exercised by its own test.
std::vector<std::pair<std::string, std::string>> collect_includes(
    const fs::path& dir)
{
    std::vector<std::pair<std::string, std::string>> includes;
    if (!fs::exists(dir)) {
        return includes;
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
            continue;
        }
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            const std::string target = include_target(line);
            if (!target.empty()) {
                includes.emplace_back(entry.path().filename().string(), target);
            }
        }
    }
    return includes;
}

/// Whether an include target hits the forbidden list.
bool is_forbidden(const std::string& target)
{
    for (const std::string_view bad : k_forbidden) {
        if (target.find(bad) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Whether a `url_shortener/...` include is on the allowed exceptions list.
bool is_allowed_url_shortener(const std::string& target)
{
    if (target.rfind("url_shortener/commands/", 0) == 0) {
        return true;
    }
    return target == "url_shortener/app/app_error.hpp"
        || target == "url_shortener/security/control_set.hpp"
        || target == "url_shortener/observability/logger.hpp";
}

/// The directory of public commands headers, or an empty path when SOURCE_DIR
/// is not defined (e.g. a direct non-CTest run).
fs::path commands_header_dir()
{
#ifdef SOURCE_DIR
    return fs::path(SOURCE_DIR) / "include" / "url_shortener" / "commands";
#else
    return {};
#endif
}

}  // namespace

// Type: unit (layering / include-hygiene guard, threat model T-18, C1/C8/C9).
// Scenario: no public header under include/url_shortener/commands/ includes a
//   forbidden dependency (transport, storage, FTXUI, core/utils.hpp, <regex>).
// Boundaries: adding `#include <boost/asio.hpp>` to a commands header fails
//   this; the scan is textual and cannot see transitive includes (compensated
//   by banning core/utils.hpp outright).
// On failure first check: which header gained a forbidden include? Replace the
//   dependency with an allowed one (std, url_shortener/commands/,
//   app_error.hpp, control_set.hpp, logger.hpp) or move the code to a .cpp.

BOOST_AUTO_TEST_CASE(commands_headers_have_no_forbidden_includes)
{
    const fs::path dir = commands_header_dir();
    if (dir.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }
    BOOST_REQUIRE_MESSAGE(
        fs::exists(dir),
        "commands header directory not found: " << dir.string());

    const auto includes = collect_includes(dir);
    // Sanity: the headers do exist and were actually read.
    BOOST_REQUIRE(!includes.empty());

    for (const auto& [file, target] : includes) {
        BOOST_TEST(!is_forbidden(target),
                   "forbidden include in " << file << ": " << target);
    }
}

// Type: unit (allowed-list sanity).
// Scenario: every `url_shortener/...` include in a commands header is one of
// the
//   explicitly allowed exceptions (commands/, app_error.hpp, control_set.hpp,
//   logger.hpp).
// Boundaries: a new cross-module url_shortener include that is neither
// forbidden
//   by name nor on the allow list still trips this check.
// On failure first check: is a new allowed dependency genuinely intended? If so
//   extend is_allowed_url_shortener and the task README; otherwise drop it.

BOOST_AUTO_TEST_CASE(commands_headers_only_allowed_url_shortener_includes)
{
    const fs::path dir = commands_header_dir();
    if (dir.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const auto includes = collect_includes(dir);
    BOOST_REQUIRE(!includes.empty());

    for (const auto& [file, target] : includes) {
        if (target.rfind("url_shortener/", 0) != 0) {
            continue;  // a std/system header; covered by the forbidden check.
        }
        BOOST_TEST(
            is_allowed_url_shortener(target),
            "unexpected url_shortener include in " << file << ": " << target);
    }
}

// Type: unit (negative self-test of the checker, threat model T-18).
// Scenario: a header containing `#include <boost/beast/http.hpp>` written to a
//   temp directory and fed to the same scanning function is reported forbidden,
//   while a clean std-only header is not.
// Boundaries: proves the guard would actually catch a real violation rather
//   than passing vacuously.
// On failure first check: did include_target or is_forbidden stop recognizing a
//   bracketed include or the boost/beast substring?

BOOST_AUTO_TEST_CASE(checker_detects_a_forbidden_include)
{
    const fs::path dir =
        fs::temp_directory_path() / "url_shortener_hygiene_selftest";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        std::ofstream bad(dir / "tainted.hpp");
        bad << "#pragma once\n";
        bad << "#include <string>\n";
        bad << "#include <boost/beast/http.hpp>\n";
    }
    {
        std::ofstream clean(dir / "clean.hpp");
        clean << "#pragma once\n";
        clean << "#include <string>\n";
        clean << "#include <url_shortener/commands/error_codes.hpp>\n";
    }

    const auto includes = collect_includes(dir);
    fs::remove_all(dir);

    bool found_forbidden = false;
    bool saw_clean_std = false;
    for (const auto& [file, target] : includes) {
        if (is_forbidden(target)) {
            found_forbidden = true;
            BOOST_TEST(target == "boost/beast/http.hpp");
            BOOST_TEST(file == "tainted.hpp");
        }
        if (target == "string") {
            saw_clean_std = true;
        }
    }

    // The checker flagged the planted violation and still read the clean file.
    BOOST_TEST(found_forbidden);
    BOOST_TEST(saw_clean_std);
}
