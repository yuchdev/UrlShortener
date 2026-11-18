#define BOOST_TEST_MODULE TuiIncludeBoundaryTest

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace
{

// The only TUI files allowed to see CommandRegistry / composition (contract
// C8): the in-process client seam and the entry point (subtask 19).
bool is_allow_listed(const fs::path& file)
{
    const std::string name = file.filename().string();
    return name == "in_process_client.hpp" || name == "in_process_client.cpp"
        || name == "tui_entry.cpp";
}

// Extracts the include target of a line, e.g. `url_shortener/app/x.hpp` from
// `#include <url_shortener/app/x.hpp>`; empty when the line is not an include.
std::string include_target(const std::string& line)
{
    static const std::regex include_re(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    std::smatch match;
    if (std::regex_search(line, match, include_re)) {
        return match[1].str();
    }
    return std::string();
}

// A header/source under the TUI tree we should scan.
bool is_scannable(const fs::path& file)
{
    const std::string ext = file.extension().string();
    return ext == ".cpp" || ext == ".hpp";
}

// Scans @p roots for include-boundary and JSON-tokenizer violations. Each
// returned string names the offending file and the reason. An empty vector
// means the tree is clean.
std::vector<std::string> collect_violations(const std::vector<fs::path>& roots)
{
    // Layer headers no TUI file may ever include (C8/C5/C10). `security/.*
    // service` targets service headers specifically, not control_set.hpp.
    static const std::regex forbidden_layer_re(
        R"(url_shortener/(app|storage|http|uri_map_singleton|)"
        R"(security/.*service))");
    // A locally-defined JSON tokenizer (C9): only json_view.cpp may wrap the
    // sanctioned scanner.
    static const std::regex tokenizer_re(
        R"((scan_[A-Za-z0-9_]*|parse_json[A-Za-z0-9_]*)\s*\()");

    std::vector<std::string> violations;
    for (const fs::path& root : roots) {
        if (!fs::exists(root)) {
            continue;
        }
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file() || !is_scannable(entry.path())) {
                continue;
            }
            const fs::path& file = entry.path();
            const std::string name = file.filename().string();
            std::ifstream in(file);
            std::string line;
            while (std::getline(in, line)) {
                const std::string target = include_target(line);
                if (!target.empty()) {
                    if (std::regex_search(target, forbidden_layer_re)) {
                        violations.push_back(file.string()
                                             + ": forbidden layer include '"
                                             + target + "'");
                    }
                    else if (target.find("url_shortener/composition")
                                 != std::string::npos
                             && !is_allow_listed(file))
                    {
                        violations.push_back(file.string()
                                             + ": composition include '"
                                             + target + "' not allow-listed");
                    }
                    else if (
                        target == "url_shortener/commands/command_registry.hpp"
                        && !is_allow_listed(file))
                    {
                        violations.push_back(
                            file.string()
                            + ": CommandRegistry include not allow-listed");
                    }
                }
                else if (file.extension() == ".cpp" && name != "json_view.cpp"
                         && std::regex_search(line, tokenizer_re))
                {
                    violations.push_back(file.string()
                                         + ": local JSON tokenizer (C9)");
                }
            }
        }
    }
    return violations;
}

// Writes @p contents to @p dir/@p name and returns the full path.
fs::path write_temp(const fs::path& dir,
                    const std::string& name,
                    const std::string& contents)
{
    const fs::path file = dir / name;
    std::ofstream out(file);
    out << contents;
    out.close();
    return file;
}

}  // namespace

// ---------------------------------------------------------------------------
// The real TUI tree passes the boundary scan.
// On failure first check: a new TUI file included a service/registry header,
// or defined its own scan_*/parse_json* function.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(real_tui_tree_is_clean)
{
#ifdef URLSHORTENER_SOURCE_DIR
    const fs::path root = URLSHORTENER_SOURCE_DIR;
    const std::vector<fs::path> roots = {
        root / "src" / "tui", root / "include" / "url_shortener" / "tui"};
    const std::vector<std::string> violations = collect_violations(roots);
    for (const std::string& violation : violations) {
        BOOST_TEST_MESSAGE("boundary violation: " << violation);
    }
    BOOST_TEST(violations.empty());
#else
    BOOST_TEST_MESSAGE("URLSHORTENER_SOURCE_DIR undefined; skipping tree scan");
    BOOST_TEST(true);
#endif
}

// ---------------------------------------------------------------------------
// A forbidden layer include is rejected by the scanner.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(forbidden_layer_include_is_rejected)
{
    const fs::path dir = fs::temp_directory_path() / "tui_boundary_forbidden";
    fs::create_directories(dir);

    write_temp(
        dir, "screen.hpp", "#include <url_shortener/storage/metadata.hpp>\n");
    const std::vector<std::string> violations = collect_violations({dir});
    BOOST_TEST(!violations.empty());

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// composition / CommandRegistry includes are rejected outside the allow-list
// and accepted inside it.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(registry_and_composition_allow_list_enforced)
{
    const fs::path dir = fs::temp_directory_path() / "tui_boundary_allowlist";
    fs::create_directories(dir);

    write_temp(dir,
               "screen.cpp",
               "#include <url_shortener/composition/factory.hpp>\n"
               "#include <url_shortener/commands/command_registry.hpp>\n");
    write_temp(dir,
               "in_process_client.cpp",
               "#include <url_shortener/composition/factory.hpp>\n"
               "#include <url_shortener/commands/command_registry.hpp>\n");

    const std::vector<std::string> violations = collect_violations({dir});
    // Exactly the two screen.cpp violations; none from the allow-listed file.
    bool screen_flagged = false;
    bool allowed_flagged = false;
    for (const std::string& violation : violations) {
        if (violation.find("screen.cpp") != std::string::npos) {
            screen_flagged = true;
        }
        if (violation.find("in_process_client.cpp") != std::string::npos) {
            allowed_flagged = true;
        }
    }
    BOOST_TEST(screen_flagged);
    BOOST_TEST(!allowed_flagged);

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// A locally-defined JSON tokenizer outside json_view.cpp is rejected (C9).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(local_json_tokenizer_is_rejected)
{
    const fs::path dir = fs::temp_directory_path() / "tui_boundary_tokenizer";
    fs::create_directories(dir);

    write_temp(dir, "other.cpp", "void scan_tokens() {}\n");
    write_temp(dir,
               "json_view.cpp",
               "#include <url_shortener/commands/json_scan.hpp>\n"
               "auto r = commands::scan_object(text);\n");

    const std::vector<std::string> violations = collect_violations({dir});
    bool other_flagged = false;
    bool json_view_flagged = false;
    for (const std::string& violation : violations) {
        if (violation.find("other.cpp") != std::string::npos) {
            other_flagged = true;
        }
        if (violation.find("json_view.cpp") != std::string::npos) {
            json_view_flagged = true;
        }
    }
    BOOST_TEST(other_flagged);
    BOOST_TEST(!json_view_flagged);

    fs::remove_all(dir);
}
