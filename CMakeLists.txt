cmake_minimum_required(VERSION 3.25)

# ---- vcpkg toolchain selection (Windows only) ----
if(WIN32)
  if(NOT CMAKE_TOOLCHAIN_FILE AND NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "Neither CMAKE_TOOLCHAIN_FILE nor VCPKG_ROOT environment variable is set. "
                        "Please provide an absolute path to the vcpkg toolchain file using "
                        "-DCMAKE_TOOLCHAIN_FILE=... or set the VCPKG_ROOT environment variable.")
  endif()

  if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain file")
  endif()
endif()

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
find_package(Boost 1.74 REQUIRED COMPONENTS program_options unit_test_framework)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
  include(FetchContent)
  FetchContent_Declare(
      yaml-cpp
      GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
      GIT_TAG 0.8.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
endif()
find_package(hiredis CONFIG QUIET)
if(hiredis_FOUND)
  set(HIREDIS_LIB hiredis::hiredis)
else()
  find_library(HIREDIS_LIB hiredis)
  if(NOT HIREDIS_LIB)
    include(FetchContent)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG v1.2.0
    )
    FetchContent_MakeAvailable(hiredis)
    set(HIREDIS_LIB hiredis)
  endif()
endif()
# ---- MSVC compile flags (must precede FetchSOCI so /FS reaches soci_core targets) ----
# /FS  - Force Synchronous PDB Writes: prevents C1041 when MSBuild compiles
#         multiple translation units of soci_core in parallel on the same .pdb file.
# /bigobj - required for Boost headers that exceed the default 65535 section limit.
if(MSVC)
    add_compile_options(/FS /bigobj)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

include(cmake/FetchSOCI.cmake)
fetch_soci_sqlite_and_postgres()

# ---- Declare executable ----
include(sources.cmake)

add_executable(url_shortener ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
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
        url_shortener_common
        OpenSSL::SSL
)

# ---- Tests ----
include(CTest)
  # ---- Core basic unit tests: standalone executables ----

  # ---- App-layer unit tests (LinkCommandService + serializers) ----

  add_library(url_shortener_common STATIC ${COMMON_CPP_SOURCES})
  target_compile_features(url_shortener_common PRIVATE cxx_std_17)
  target_include_directories(url_shortener_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(url_shortener_common
    PUBLIC
        Boost::program_options
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        SOCI::soci_core
        SOCI::soci_sqlite3
        SOCI::soci_postgresql
        ${HIREDIS_LIB}
        yaml-cpp::yaml-cpp
  )

  add_executable(route_registry_dump
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/route_registry_dump.cpp
  )
  target_compile_features(route_registry_dump PRIVATE cxx_std_17)
  target_include_directories(route_registry_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(route_registry_dump PRIVATE url_shortener_common)

  if(BUILD_TESTING)

  foreach(unit_src IN LISTS ALL_UNIT_SOURCES)
    get_filename_component(unit_name ${unit_src} NAME_WE)
    add_executable(${unit_name}
        ${CMAKE_CURRENT_SOURCE_DIR}/${unit_src}
    )
    target_compile_features(${unit_name} PRIVATE cxx_std_17)
    target_include_directories(${unit_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${unit_name}
      PRIVATE
        url_shortener_common
        Boost::unit_test_framework
    )
    add_test(NAME ${unit_name} COMMAND ${unit_name})
    set_tests_properties(${unit_name} PROPERTIES LABELS "unit")
  endforeach()

  # Helper macro to register C++ tests with unique target names (parent_dir__filename)
  # and the appropriate CTest label so suites can be run independently.
  macro(register_labeled_cpp_tests src_list label)
    foreach(cpp_test_src IN LISTS ${src_list})
      get_filename_component(cpp_test_name   ${cpp_test_src} NAME_WE)
      get_filename_component(cpp_test_dir    ${cpp_test_src} DIRECTORY)
      get_filename_component(cpp_test_parent ${cpp_test_dir} NAME)
      set(cpp_test_target "${cpp_test_parent}__${cpp_test_name}")
      add_executable(${cpp_test_target}
          ${CMAKE_CURRENT_SOURCE_DIR}/${cpp_test_src}
      )
      target_compile_features(${cpp_test_target} PRIVATE cxx_std_17)
      target_include_directories(${cpp_test_target} PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/metadata
      )
      target_link_libraries(${cpp_test_target}
        PRIVATE
          url_shortener_common
          Boost::unit_test_framework
      )
      add_test(NAME ${cpp_test_target} COMMAND ${cpp_test_target})
      set_tests_properties(${cpp_test_target} PROPERTIES LABELS "${label}")
    endforeach()
  endmacro()

  register_labeled_cpp_tests(SECURITY_UNIT_SOURCES           "unit")
  register_labeled_cpp_tests(CLI_UNIT_SOURCES            "unit")
  register_labeled_cpp_tests(CORE_UNIT_SOURCES           "unit")
  register_labeled_cpp_tests(INMEMORY_UNIT_SOURCES       "unit")
  register_labeled_cpp_tests(SQL_FOUNDATION_UNIT_SOURCES "unit")
  register_labeled_cpp_tests(SQLITE_UNIT_SOURCES         "unit")
  register_labeled_cpp_tests(PSQL_UNIT_SOURCES           "unit")
  register_labeled_cpp_tests(RATELIMITER_UNIT_SOURCES    "unit")
  register_labeled_cpp_tests(REDIS_UNIT_SOURCES          "unit")
  register_labeled_cpp_tests(OBSERVABILITY_UNIT_SOURCES  "unit")
  register_labeled_cpp_tests(DOCS_UNIT_SOURCES           "unit")
  target_compile_definitions(docs__01_configuration_examples_match_parser
      PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  register_labeled_cpp_tests(ANALYTICS_UNIT_SOURCES      "unit")
  register_labeled_cpp_tests(ANALYTICS_INTEGRATION_SOURCES "integration")
  register_labeled_cpp_tests(CONTRACT_METADATA_SOURCES   "contract")
  register_labeled_cpp_tests(STORAGE_INTEGRATION_SOURCES "integration")
  register_labeled_cpp_tests(SQLITE_INTEGRATION_SOURCES  "integration")
  register_labeled_cpp_tests(PSQL_INTEGRATION_SOURCES    "integration")
  register_labeled_cpp_tests(REDIS_INTEGRATION_SOURCES   "integration")

  # ---- App-layer unit tests: dedicated loop with tests/unit/app include dir ----
  # These tests use AppTestFakes.hpp from tests/unit/app/, so the test directory
  # must be on the include path (the generic macro uses tests/contract/metadata).
  foreach(app_unit_src IN LISTS APP_UNIT_SOURCES)
    get_filename_component(app_test_name   ${app_unit_src} NAME_WE)
    set(app_test_target "app__${app_test_name}")
    add_executable(${app_test_target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${app_unit_src}
    )
    target_compile_features(${app_test_target} PRIVATE cxx_std_17)
    target_include_directories(${app_test_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/app
    )
    target_link_libraries(${app_test_target}
      PRIVATE
        url_shortener_common
        Boost::unit_test_framework
    )
    add_test(NAME ${app_test_target} COMMAND ${app_test_target})
    set_tests_properties(${app_test_target} PROPERTIES LABELS "unit")
  endforeach()

  # ---- Core integration tests: standalone executables (Python scripts) ----

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_parser_test/cli_parser_test.cpp)
    add_executable(cli_parser_test
        ${CMAKE_CURRENT_SOURCE_DIR}/test/cli_parser_test/cli_parser_test.cpp
    )
    target_compile_features(cli_parser_test PRIVATE cxx_std_17)
    target_include_directories(cli_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(cli_parser_test
      PRIVATE
        Boost::program_options
        Boost::unit_test_framework
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    )
    add_test(NAME cli_parser_test COMMAND cli_parser_test)
    set_tests_properties(cli_parser_test PROPERTIES LABELS "unit")
  endif()

  # ---- Stage 1 integration tests: 8 standalone executables (Python scripts) ----

  # ---- CLI integration tests (spawn binary in one-shot CLI mode) ----

  foreach(integration_src IN LISTS ALL_INTEGRATION_SOURCES)
    get_filename_component(integration_name ${integration_src} NAME_WE)
    add_test(
      NAME ${integration_name}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${integration_src}
    )
    set_tests_properties(${integration_name} PROPERTIES
      ENVIRONMENT
        "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>;URLSHORTENER_CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE};URLSHORTENER_CTEST_CONFIG=$<CONFIG>;URLSHORTENER_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR};URLSHORTENER_VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
      TIMEOUT 600
      LABELS "integration"
    )
  endforeach()

  # ---- CLI integration tests (one-shot CLI mode; no server port needed) ----
  foreach(cli_src IN LISTS CLI_INTEGRATION_SOURCES)
    get_filename_component(cli_test_name ${cli_src} NAME_WE)
    add_test(
      NAME cli_${cli_test_name}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${cli_src}
    )
    set_tests_properties(cli_${cli_test_name} PROPERTIES
      ENVIRONMENT
        "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>"
      TIMEOUT 60
      LABELS "integration"
    )
  endforeach()

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/http_client_test/http_client_test.py)
    add_test(
      NAME http_client_test
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/http_client_test/http_client_test.py
    )
    set_tests_properties(http_client_test PROPERTIES
      ENVIRONMENT "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>"
      TIMEOUT 180
      LABELS "integration"
    )
  endif()

  # ---- E2E shell scenario tests (label "e2e") ----
  # The section scripts under tests/e2e/scripts/ are POSIX-shell + live-service
  # scenarios (they read /proc and drive a background HTTP service), so they are
  # registered only on non-Windows hosts where a bash interpreter is available.
  # Each section binds the same fixed port and shares tests/e2e/tmp, so they are
  # marked RUN_SERIAL to prevent parallel interference. Registering them here lets
  # a single ctest invocation cover every category, e.g.:
  #   ctest -L "unit|contract|integration|e2e"
  option(URLSHORTENER_ENABLE_E2E_TESTS
      "Register shell-based e2e scenario tests with CTest" ON)
  if(URLSHORTENER_ENABLE_E2E_TESTS AND NOT CMAKE_HOST_WIN32)
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
      foreach(e2e_section IN LISTS E2E_SECTIONS)
        add_test(
          NAME e2e_${e2e_section}
          COMMAND ${BASH_EXECUTABLE}
                  ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/scripts/run_section.sh
                  ${e2e_section}
          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(e2e_${e2e_section} PROPERTIES
          LABELS "e2e"
          RUN_SERIAL TRUE
          TIMEOUT 120
        )
      endforeach()

      # ---- CLI e2e sections (standalone; no mock service; real binary) ----
      # These sections run the actual url_shortener binary in one-shot CLI mode
      # and assert stdout JSON and exit behaviour.  They are independent of the
      # mock HTTP service sections above.
      foreach(cli_section IN LISTS E2E_CLI_SECTIONS)
        add_test(
          NAME e2e_${cli_section}
          COMMAND ${BASH_EXECUTABLE}
                  ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/scripts/sections/${cli_section}.sh
          WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(e2e_${cli_section} PROPERTIES
          ENVIRONMENT "URLSHORTENER_BIN=$<TARGET_FILE:url_shortener>"
          LABELS "e2e"
          RUN_SERIAL TRUE
          TIMEOUT 30
        )
      endforeach()
    else()
      message(STATUS
          "URLSHORTENER_ENABLE_E2E_TESTS is ON but no bash interpreter was found; "
          "skipping e2e CTest registration.")
    endif()
  endif()
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
