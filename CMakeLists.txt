cmake_minimum_required(VERSION 3.25)

include(cmake/prelude.cmake)

project(
    url_shortener
    VERSION 1.0.0
    DESCRIPTION "HTTPS URL Shortener Server"
    HOMEPAGE_URL "https://github.com/yuchdev/UrlShortener"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

set(CMAKE_CXX_STANDARD 17)

# ---- Dependencies (Ubuntu system packages) ----
find_package(Boost 1.74 REQUIRED COMPONENTS system program_options unit_test_framework)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ---- Declare executable ----
include(sources.cmake)

add_executable(url_shortener ${URL_SHORTENER_SOURCES})
add_executable(url_shortener::exe ALIAS url_shortener)

set_property(TARGET url_shortener PROPERTY OUTPUT_NAME url_shortener)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

target_compile_features(url_shortener PRIVATE cxx_std_17)

target_include_directories(url_shortener
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(url_shortener
    PRIVATE
        Boost::system
        Boost::program_options
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

# ---- Tests ----
include(CTest)
if(BUILD_TESTING)
  add_test(
    NAME http_client_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/http_client_test/http_client_test.py
  )
  set_tests_properties(http_client_test PROPERTIES
    ENVIRONMENT "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>"
    TIMEOUT 180
  )

  # ---- CLI parser unit tests (Boost.Test) ----
  add_executable(cli_parser_test
      ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_parser_test/cli_parser_test.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cli_parser.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/url_shortener.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
  )

  target_compile_features(cli_parser_test PRIVATE cxx_std_17)

  target_include_directories(cli_parser_test
      PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  target_link_libraries(cli_parser_test
      PRIVATE
          Boost::system
          Boost::program_options
          Boost::unit_test_framework
          OpenSSL::SSL
          OpenSSL::Crypto
          Threads::Threads
  )

  add_test(NAME cli_parser_test COMMAND cli_parser_test)

  add_executable(shortener_core_unit_test
      ${CMAKE_CURRENT_SOURCE_DIR}/test/shortener_core_unit_test.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cli_parser.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
  )

  target_compile_features(shortener_core_unit_test PRIVATE cxx_std_17)

  target_include_directories(shortener_core_unit_test
      PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  target_link_libraries(shortener_core_unit_test
      PRIVATE
          Boost::system
          Boost::program_options
          Boost::unit_test_framework
          OpenSSL::SSL
          OpenSSL::Crypto
          Threads::Threads
  )

  add_test(NAME shortener_core_unit_test COMMAND shortener_core_unit_test)
endif()

# ---- Install rules ----
if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----
if(NOT url_shortener_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of url_shortener"
  )
endif()

include(cmake/dev-mode.cmake)
