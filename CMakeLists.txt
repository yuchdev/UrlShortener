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
include(cmake/FetchSOCI.cmake)
fetch_soci_sqlite_only()

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

  set(STAGE3_CORE_UNIT_SOURCES
      test/unit/core/01_result_error_taxonomy.cpp
      test/unit/core/02_link_record_validation.cpp
      test/unit/core/03_cache_value_model.cpp
      test/unit/core/04_link_access_event_model.cpp
      test/unit/core/05_interface_smoke_compile.cpp
  )

  set(STAGE3_INMEMORY_UNIT_SOURCES
      test/unit/storage/inmemory/01_create_and_get_roundtrip.cpp
      test/unit/storage/inmemory/02_duplicate_short_code.cpp
      test/unit/storage/inmemory/03_get_unknown.cpp
      test/unit/storage/inmemory/04_update_existing.cpp
      test/unit/storage/inmemory/05_delete_existing.cpp
      test/unit/storage/inmemory/06_exists_consistency.cpp
      test/unit/storage/inmemory/07_list_filtering_and_pagination.cpp
      test/unit/storage/inmemory/08_expiry_semantics.cpp
      test/unit/storage/inmemory/09_thread_safety_smoke.cpp
      test/unit/storage/inmemory/10_cache_get_set_delete.cpp
      test/unit/storage/inmemory/11_cache_ttl_lazy_eviction.cpp
      test/unit/storage/inmemory/12_cache_clear_by_prefix.cpp
      test/unit/storage/inmemory/13_cache_fail_open_behavior.cpp
      test/unit/storage/inmemory/14_analytics_emit_and_flush.cpp
      test/unit/storage/inmemory/15_rate_limit_allow_window.cpp
  )

  set(STAGE3_CONTRACT_METADATA_SOURCES
      test/contract/metadata/01_create_get_roundtrip.cpp
      test/contract/metadata/02_duplicate_short_code.cpp
      test/contract/metadata/03_get_unknown.cpp
      test/contract/metadata/04_update_existing.cpp
      test/contract/metadata/05_delete_removes_record.cpp
      test/contract/metadata/06_list_filtering_pagination.cpp
      test/contract/metadata/07_expiry_semantics.cpp
      test/contract/metadata/08_concurrent_create_one_wins.cpp
  )

  set(STAGE3_STORAGE_INTEGRATION_SOURCES
      test/integration/storage/01_inmemory_resolve_full_flow.cpp
      test/integration/storage/02_inmemory_cache_aside.cpp
      test/integration/storage/03_inmemory_cache_invalidation_after_update.cpp
      test/integration/storage/04_inmemory_cache_invalidation_after_delete.cpp
      test/integration/storage/05_inmemory_redirect_visible_states.cpp
      test/integration/storage/06_inmemory_analytics_best_effort.cpp
      test/integration/storage/07_sqlite_service_full_flow.cpp
  )

  set(STAGE3_SQL_FOUNDATION_UNIT_SOURCES
      test/unit/storage/sql/01_connection_config_defaults.cpp
      test/unit/storage/sql/02_backend_kind_selection.cpp
      test/unit/storage/sql/03_sqlite_dialect_schema_sql.cpp
      test/unit/storage/sql/04_row_mapper_roundtrip.cpp
      test/unit/storage/sql/05_repository_uses_dialect_and_factory.cpp
      test/unit/storage/sql/06_error_mapper_interface_contract.cpp
  )

  set(STAGE3_SQLITE_UNIT_SOURCES
      test/unit/storage/sqlite/01_schema_bootstrap.cpp
      test/unit/storage/sqlite/02_create_and_get_roundtrip.cpp
      test/unit/storage/sqlite/03_duplicate_short_code_maps_to_already_exists.cpp
      test/unit/storage/sqlite/04_get_unknown_returns_empty.cpp
      test/unit/storage/sqlite/05_update_existing.cpp
      test/unit/storage/sqlite/06_delete_existing.cpp
      test/unit/storage/sqlite/07_exists_consistency.cpp
      test/unit/storage/sqlite/08_attributes_json_roundtrip.cpp
      test/unit/storage/sqlite/09_optional_fields_roundtrip.cpp
      test/unit/storage/sqlite/10_timestamp_roundtrip.cpp
      test/unit/storage/sqlite/11_busy_timeout_configuration.cpp
      test/unit/storage/sqlite/12_error_mapping_generic_failure.cpp
      test/unit/storage/sqlite/13_session_factory_builds_sqlite_session.cpp
      test/unit/storage/sqlite/14_sql_repository_with_sqlite_backend.cpp
  )

  set(STAGE3_SQLITE_INTEGRATION_SOURCES
      test/integration/sqlite/03_contract_suite_runner.cpp
  )

  set(STAGE3_COMMON_CPP_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/LinkService.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/inmemory/InMemoryMetadataRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/inmemory/InMemoryCacheStore.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/inmemory/InMemoryAnalyticsSink.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/inmemory/InMemoryRateLimiter.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sql/SqlMetadataRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sql/SqlMetadataRowMapper.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sqlite/SqliteSqlDialect.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sqlite/SqliteSessionFactory.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sqlite/SqliteErrorMapper.cpp
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



  # Helper macro to register Stage-3 C++ tests with unique target names (parent_dir__filename)
  # and the appropriate CTest label so suites can be run independently.
  macro(register_stage3_cpp_tests src_list label)
    foreach(stage3_src IN LISTS ${src_list})
      get_filename_component(stage3_name   ${stage3_src} NAME_WE)
      get_filename_component(stage3_dir    ${stage3_src} DIRECTORY)
      get_filename_component(stage3_parent ${stage3_dir} NAME)
      set(stage3_target "${stage3_parent}__${stage3_name}")
      add_executable(${stage3_target}
          ${CMAKE_CURRENT_SOURCE_DIR}/${stage3_src}
          ${STAGE3_COMMON_CPP_SOURCES}
      )
      target_compile_features(${stage3_target} PRIVATE cxx_std_17)
      target_include_directories(${stage3_target} PRIVATE
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${CMAKE_CURRENT_SOURCE_DIR}/test/contract/metadata
      )
      target_link_libraries(${stage3_target}
        PRIVATE
          Boost::system
          Boost::program_options
          Boost::unit_test_framework
          OpenSSL::SSL
          OpenSSL::Crypto
          Threads::Threads
          SOCI::soci_core
          SOCI::soci_sqlite3
      )
      add_test(NAME ${stage3_target} COMMAND ${stage3_target})
      set_tests_properties(${stage3_target} PROPERTIES LABELS "${label}")
    endforeach()
  endmacro()

  register_stage3_cpp_tests(STAGE3_CORE_UNIT_SOURCES           "unit")
  register_stage3_cpp_tests(STAGE3_INMEMORY_UNIT_SOURCES       "unit")
  register_stage3_cpp_tests(STAGE3_SQL_FOUNDATION_UNIT_SOURCES "unit")
  register_stage3_cpp_tests(STAGE3_SQLITE_UNIT_SOURCES         "unit")
  register_stage3_cpp_tests(STAGE3_CONTRACT_METADATA_SOURCES   "contract")
  register_stage3_cpp_tests(STAGE3_STORAGE_INTEGRATION_SOURCES "integration")
  register_stage3_cpp_tests(STAGE3_SQLITE_INTEGRATION_SOURCES  "integration")
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

  set(STAGE3_BUILD_INTEGRATION_SOURCES
      test/integration/build/01_fetch_soci_sqlite_only.py
      test/integration/build/02_sqlite_backend_enabled.py
      test/integration/sqlite/01_db_file_initialization.py
      test/integration/sqlite/02_busy_lock_behavior.py
  )

  set(ALL_INTEGRATION_SOURCES
      ${STAGE1_INTEGRATION_SOURCES}
      ${STAGE2_LINK_MANAGEMENT_INTEGRATION_SOURCES}
      ${STAGE3_BUILD_INTEGRATION_SOURCES}
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
