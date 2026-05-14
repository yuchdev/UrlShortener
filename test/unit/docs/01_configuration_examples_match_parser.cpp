#define BOOST_TEST_MODULE 01_configuration_examples_match_parser
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "url_shortener/config/StorageConfig.hpp"

BOOST_AUTO_TEST_CASE(all_yaml_examples_parse) {
    std::ifstream in("docs/storage/configuration.md");
    BOOST_REQUIRE(in.good());
    std::stringstream ss; ss << in.rdbuf();
    const std::string content = ss.str();

    std::regex block("```yaml\\n([\\s\\S]*?)```", std::regex::icase);
    std::sregex_iterator begin(content.begin(), content.end(), block), end;
    BOOST_REQUIRE(begin != end);
    for (auto it = begin; it != end; ++it) {
        const auto yaml = (*it)[1].str();
        BOOST_CHECK_NO_THROW(ParseStorageConfigYaml(yaml));
    }
}
