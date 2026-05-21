#define BOOST_TEST_MODULE 01_configuration_examples_match_parser
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "url_shortener/config/StorageConfig.hpp"

namespace {
std::filesystem::path findConfigurationDoc() {
    namespace fs = std::filesystem;
    fs::path cur = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const auto candidate = cur / "docs" / "storage" / "configuration.md";
        if (fs::exists(candidate)) {
            return candidate;
        }
        if (!cur.has_parent_path()) {
            break;
        }
        cur = cur.parent_path();
    }
    return {};
}
} // namespace

BOOST_AUTO_TEST_CASE(all_yaml_examples_parse) {
    const auto doc_path = findConfigurationDoc();
    BOOST_REQUIRE_MESSAGE(!doc_path.empty(), "could not locate docs/storage/configuration.md from cwd");

    std::ifstream in(doc_path);
    BOOST_REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();

    std::regex block("```yaml\\n([\\s\\S]*?)```", std::regex::icase);
    std::sregex_iterator begin(content.begin(), content.end(), block), end;
    BOOST_REQUIRE(begin != end);
    for (auto it = begin; it != end; ++it) {
        const auto yaml = (*it)[1].str();
        BOOST_CHECK_NO_THROW(ParseStorageConfigYaml(yaml));
    }
}
