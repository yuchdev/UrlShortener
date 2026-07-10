#define BOOST_TEST_MODULE 01_configuration_examples_match_parser
#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "url_shortener/config/StorageConfig.hpp"

BOOST_AUTO_TEST_CASE(all_yaml_examples_parse) {
#ifndef SOURCE_DIR
    BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
    return;
#else
    const std::filesystem::path doc_path =
        std::filesystem::path(SOURCE_DIR) / "docs" / "storage" / "configuration.md";
    BOOST_REQUIRE_MESSAGE(std::filesystem::exists(doc_path),
                          "could not locate docs/storage/configuration.md at " + doc_path.string());

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
#endif
}
