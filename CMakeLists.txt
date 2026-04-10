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
  # ---- Stage 1 unit tests: 8 standalone executables ----
  set(STAGE1_UNIT_SOURCES
      test/unit/core/01_url_validation_accept_test.cpp
      test/unit/core/02_url_validation_reject_test.cpp
      test/unit/core/03_url_normalization_test.cpp
      test/unit/core/04_slug_accept_test.cpp
      test/unit/core/05_slug_reject_test.cpp
      test/unit/core/06_slug_collision_retry_test.cpp
      test/unit/core/07_expiry_logic_test.cpp
      test/unit/core/08_redirect_status_test.cpp
  )

  set(STAGE2_LINK_MANAGEMENT_UNIT_SOURCES
      test/unit/link_management/01_status_precedence_test.cpp
      test/unit/link_management/02_tag_validation_test.cpp
      test/unit/link_management/03_metadata_constraints_test.cpp
      test/unit/link_management/04_campaign_validation_test.cpp
      test/unit/link_management/05_reserved_slug_test.cpp
      test/unit/link_management/06_hooks_default_behavior_test.cpp
      test/unit/link_management/07_stats_increment_test.cpp
  )

  set(ALL_UNIT_SOURCES
      ${STAGE1_UNIT_SOURCES}
      ${STAGE2_LINK_MANAGEMENT_UNIT_SOURCES}
  )

  foreach(unit_src IN LISTS ALL_UNIT_SOURCES)
    get_filename_component(unit_name ${unit_src} NAME_WE)
    add_executable(${unit_name}
        ${CMAKE_CURRENT_SOURCE_DIR}/${unit_src}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
    )
    target_compile_features(${unit_name} PRIVATE cxx_std_17)
    target_include_directories(${unit_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${unit_name}
      PRIVATE
        Boost::system
        Boost::program_options
        Boost::unit_test_framework
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    )
    add_test(NAME ${unit_name} COMMAND ${unit_name})
    set_tests_properties(${unit_name} PROPERTIES LABELS "unit")
  endforeach()

  # ---- Stage 1 integration tests: 8 standalone executables (Python scripts) ----
  set(STAGE1_INTEGRATION_SOURCES
      test/integration/core/01_generated_slug_redirect_test.py
      test/integration/core/02_custom_slug_get_by_id_slug_test.py
      test/integration/core/03_duplicate_slug_conflict_test.py
      test/integration/core/04_disabled_redirect_test.py
      test/integration/core/05_expired_redirect_test.py
      test/integration/core/06_missing_slug_not_found_test.py
      test/integration/core/07_permanent_temporary_status_test.py
      test/integration/core/08_short_url_base_domain_once_test.py
  )

  set(STAGE2_LINK_MANAGEMENT_INTEGRATION_SOURCES
      test/integration/link_management/01_create_with_management_fields_test.py
      test/integration/link_management/02_disable_redirect_blocked_test.py
      test/integration/link_management/03_enable_redirect_success_test.py
      test/integration/link_management/04_soft_delete_management_visibility_test.py
      test/integration/link_management/05_restore_redirect_success_test.py
      test/integration/link_management/06_expired_redirect_blocked_test.py
      test/integration/link_management/07_preview_status_matrix_test.py
      test/integration/link_management/08_stats_endpoint_structure_test.py
      test/integration/link_management/09_reserved_slug_conflict_test.py
      test/integration/link_management/10_backward_compatibility_flow_test.py
  )

  set(ALL_INTEGRATION_SOURCES
      ${STAGE1_INTEGRATION_SOURCES}
      ${STAGE2_LINK_MANAGEMENT_INTEGRATION_SOURCES}
  )

  foreach(integration_src IN LISTS ALL_INTEGRATION_SOURCES)
    get_filename_component(integration_name ${integration_src} NAME_WE)
    add_test(
      NAME ${integration_name}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${integration_src}
    )
    set_tests_properties(${integration_name} PROPERTIES
      ENVIRONMENT "SIMPLE_HTTP_BIN=$<TARGET_FILE:url_shortener>"
      TIMEOUT 180
      LABELS "integration"
    )
  endforeach()
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
