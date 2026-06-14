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
find_package(Boost 1.74 REQUIRED COMPONENTS system program_options unit_test_framework)
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
include(cmake/FetchSOCI.cmake)
fetch_soci_sqlite_and_postgres()

# ---- Declare executable ----
include(sources.cmake)

add_executable(url_shortener ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(url_shortener::exe ALIAS url_shortener)

set_property(TARGET url_shortener PROPERTY OUTPUT_NAME url_shortener)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

target_compile_features(url_shortener PRIVATE cxx_std_17)

if(MSVC)
    add_compile_options(/bigobj)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

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
  # ---- Stage 1 unit tests: 8 standalone executables ----
  set(CORE_BASIC_UNIT_SOURCES
      tests/unit/core/01_url_validation_accept_test.cpp
      tests/unit/core/02_url_validation_reject_test.cpp
      tests/unit/core/03_url_normalization_test.cpp
      tests/unit/core/04_slug_accept_test.cpp
      tests/unit/core/05_slug_reject_test.cpp
      tests/unit/core/06_slug_collision_retry_test.cpp
      tests/unit/core/07_expiry_logic_test.cpp
      tests/unit/core/08_redirect_status_test.cpp
  )

  set(LINK_MANAGEMENT_UNIT_SOURCES
      tests/unit/link_management/01_status_precedence_test.cpp
      tests/unit/link_management/02_tag_validation_test.cpp
      tests/unit/link_management/03_metadata_constraints_test.cpp
      tests/unit/link_management/04_campaign_validation_test.cpp
      tests/unit/link_management/05_reserved_slug_test.cpp
      tests/unit/link_management/06_hooks_default_behavior_test.cpp
      tests/unit/link_management/07_stats_increment_test.cpp
  )

  set(CORE_UNIT_SOURCES
      tests/unit/core/01_result_error_taxonomy.cpp
      tests/unit/core/02_link_record_validation.cpp
      tests/unit/core/03_cache_value_model.cpp
      tests/unit/core/04_link_access_event_model.cpp
      tests/unit/core/05_interface_smoke_compile.cpp
  )

  set(INMEMORY_UNIT_SOURCES
      tests/unit/storage/inmemory/01_create_and_get_roundtrip.cpp
      tests/unit/storage/inmemory/02_duplicate_short_code.cpp
      tests/unit/storage/inmemory/03_get_unknown.cpp
      tests/unit/storage/inmemory/04_update_existing.cpp
      tests/unit/storage/inmemory/05_delete_existing.cpp
      tests/unit/storage/inmemory/06_exists_consistency.cpp
      tests/unit/storage/inmemory/07_list_filtering_and_pagination.cpp
      tests/unit/storage/inmemory/08_expiry_semantics.cpp
      tests/unit/storage/inmemory/09_thread_safety_smoke.cpp
      tests/unit/storage/inmemory/10_cache_get_set_delete.cpp
      tests/unit/storage/inmemory/11_cache_ttl_lazy_eviction.cpp
      tests/unit/storage/inmemory/12_cache_clear_by_prefix.cpp
      tests/unit/storage/inmemory/13_cache_fail_open_behavior.cpp
      tests/unit/storage/inmemory/14_analytics_emit_and_flush.cpp
      tests/unit/storage/inmemory/15_rate_limit_allow_window.cpp
  )

  set(CONTRACT_METADATA_SOURCES
      tests/contract/metadata/01_create_get_roundtrip.cpp
      tests/contract/metadata/02_duplicate_short_code.cpp
      tests/contract/metadata/03_get_unknown.cpp
      tests/contract/metadata/04_update_existing.cpp
      tests/contract/metadata/05_delete_removes_record.cpp
      tests/contract/metadata/06_list_filtering_pagination.cpp
      tests/contract/metadata/07_expiry_semantics.cpp
      tests/contract/metadata/08_concurrent_create_one_wins.cpp
  )

  set(STORAGE_INTEGRATION_SOURCES
      tests/integration/storage/01_inmemory_resolve_full_flow.cpp
      tests/integration/storage/02_inmemory_cache_aside.cpp
      tests/integration/storage/03_inmemory_cache_invalidation_after_update.cpp
      tests/integration/storage/04_inmemory_cache_invalidation_after_delete.cpp
      tests/integration/storage/05_inmemory_redirect_visible_states.cpp
      tests/integration/storage/06_inmemory_analytics_best_effort.cpp
      tests/integration/storage/07_sqlite_service_full_flow.cpp
  )

  set(SQL_FOUNDATION_UNIT_SOURCES
      tests/unit/storage/sql/01_connection_config_defaults.cpp
      tests/unit/storage/sql/02_backend_kind_selection.cpp
      tests/unit/storage/sql/03_sqlite_dialect_schema_sql.cpp
      tests/unit/storage/sql/04_row_mapper_roundtrip.cpp
      tests/unit/storage/sql/05_repository_uses_dialect_and_factory.cpp
      tests/unit/storage/sql/06_error_mapper_interface_contract.cpp
      tests/unit/storage/sql/07_backend_registry_supports_sqlite_and_postgres.cpp
      tests/unit/storage/sql/08_postgres_config_roundtrip.cpp
      tests/unit/storage/sql/09_sql_repository_backend_switching.cpp
  )

  set(SQLITE_UNIT_SOURCES
      tests/unit/storage/sqlite/01_schema_bootstrap.cpp
      tests/unit/storage/sqlite/02_create_and_get_roundtrip.cpp
      tests/unit/storage/sqlite/03_duplicate_short_code_maps_to_already_exists.cpp
      tests/unit/storage/sqlite/04_get_unknown_returns_empty.cpp
      tests/unit/storage/sqlite/05_update_existing.cpp
      tests/unit/storage/sqlite/06_delete_existing.cpp
      tests/unit/storage/sqlite/07_exists_consistency.cpp
      tests/unit/storage/sqlite/08_attributes_json_roundtrip.cpp
      tests/unit/storage/sqlite/09_optional_fields_roundtrip.cpp
      tests/unit/storage/sqlite/10_timestamp_roundtrip.cpp
      tests/unit/storage/sqlite/11_busy_timeout_configuration.cpp
      tests/unit/storage/sqlite/12_error_mapping_generic_failure.cpp
      tests/unit/storage/sqlite/13_session_factory_builds_sqlite_session.cpp
      tests/unit/storage/sqlite/14_sql_repository_with_sqlite_backend.cpp
  )

  set(SQLITE_INTEGRATION_SOURCES
      tests/integration/sqlite/03_contract_suite_runner.cpp
  )

  set(PSQL_UNIT_SOURCES
      tests/unit/storage/psql/01_dsn_and_connection_options.cpp
      tests/unit/storage/psql/02_create_and_get_roundtrip.cpp
      tests/unit/storage/psql/03_duplicate_short_code_maps_to_already_exists.cpp
      tests/unit/storage/psql/04_get_unknown_returns_empty.cpp
      tests/unit/storage/psql/05_update_existing.cpp
      tests/unit/storage/psql/06_delete_existing.cpp
      tests/unit/storage/psql/07_exists_consistency.cpp
      tests/unit/storage/psql/08_jsonb_attributes_roundtrip.cpp
      tests/unit/storage/psql/09_optional_fields_roundtrip.cpp
      tests/unit/storage/psql/10_error_mapping_transient_vs_permanent.cpp
      tests/unit/storage/psql/11_prepared_statement_registration.cpp
      tests/unit/storage/psql/12_retry_policy_bounds.cpp
      tests/unit/storage/psql/13_migration_runner_ordering.cpp
      tests/unit/storage/psql/14_sql_repository_with_postgres_backend.cpp
  )

  set(PSQL_INTEGRATION_SOURCES
      tests/integration/psql/05_contract_suite_runner.cpp
      tests/integration/storage/08_postgres_service_full_flow.cpp
  )

  set(RATELIMITER_UNIT_SOURCES
      tests/unit/storage/ratelimiter/01_allow_under_limit.cpp
      tests/unit/storage/ratelimiter/02_deny_over_limit.cpp
      tests/unit/storage/ratelimiter/03_boundary_at_exact_limit.cpp
      tests/unit/storage/ratelimiter/04_window_reset.cpp
      tests/unit/storage/ratelimiter/05_key_format.cpp
      tests/unit/storage/ratelimiter/06_error_mapping.cpp
      tests/unit/storage/ratelimiter/07_invalid_configuration.cpp
  )

  set(REDIS_UNIT_SOURCES
      tests/unit/storage/redis/01_cache_key_format.cpp
      tests/unit/storage/redis/02_cache_value_serialization.cpp
      tests/unit/storage/redis/03_cache_value_deserialization.cpp
      tests/unit/storage/redis/04_set_get_delete_commands.cpp
      tests/unit/storage/redis/05_ttl_conversion.cpp
      tests/unit/storage/redis/06_clear_by_prefix_strategy.cpp
      tests/unit/storage/redis/07_fail_open_error_mapping.cpp
      tests/unit/storage/redis/08_schema_version_compatibility.cpp
  )

  set(OBSERVABILITY_UNIT_SOURCES
      tests/unit/observability/01_repository_metrics_labels.cpp
      tests/unit/observability/02_cache_metrics_labels.cpp
      tests/unit/observability/03_startup_logging_redaction.cpp
      tests/unit/docs/01_configuration_examples_match_parser.cpp
  )

  set(CONFIG_UNIT_SOURCES
      tests/unit/config/01_parse_minimal_inmemory.cpp
      tests/unit/config/02_parse_sqlite_config.cpp
      tests/unit/config/03_parse_postgres_config.cpp
      tests/unit/config/04_parse_redis_cache_config.cpp
      tests/unit/config/05_invalid_backend_name.cpp
      tests/unit/config/06_missing_required_fields.cpp
      tests/unit/config/07_default_values.cpp
      tests/unit/config/08_factory_selects_correct_backend_types.cpp
  )


  set(ANALYTICS_UNIT_SOURCES
      tests/unit/analytics/01_click_event_model_validation.cpp
      tests/unit/analytics/02_click_event_builder_required_fields.cpp
      tests/unit/analytics/03_click_event_sanitizer_limits.cpp
      tests/unit/analytics/04_client_id_hasher.cpp
      tests/unit/analytics/05_aggregate_query_validator.cpp
      tests/unit/analytics/06_bounded_click_event_queue_basic.cpp
      tests/unit/analytics/07_bounded_click_event_queue_overflow.cpp
      tests/unit/analytics/08_analytics_service_failure_isolation.cpp
      tests/unit/analytics/09_analytics_metrics.cpp
      tests/unit/analytics/10_analytics_worker_batch_flush.cpp
      tests/unit/analytics/11_analytics_worker_interval_flush.cpp
      tests/unit/analytics/12_analytics_worker_retry_bounds.cpp
      tests/unit/analytics/13_retention_cleanup_job.cpp
      tests/unit/analytics/14_analytics_config_loading.cpp
      tests/unit/analytics/15_analytics_disabled_mode.cpp
      tests/unit/analytics/16_analytics_worker_start_probe_uaf.cpp
      tests/unit/http/analytics/03_redirect_analytics_hook.cpp
      tests/unit/http/analytics/04_stats_handler_validation.cpp
      tests/unit/storage/memory/01_in_memory_click_event_repository.cpp
      tests/unit/storage/memory/02_in_memory_click_event_repository_buckets.cpp
      tests/unit/storage/memory/03_in_memory_click_event_repository_retention.cpp
      tests/unit/http/analytics/01_stats_handler_validation.cpp
      tests/unit/http/analytics/02_stats_handler_response_mapping.cpp
      tests/unit/storage/memory/16_in_memory_click_event_repository.cpp
  )

  set(ANALYTICS_INTEGRATION_SOURCES
      tests/integration/cpp/analytics/01_redirect_success_emits_click_event.cpp
      tests/integration/cpp/analytics/02_redirect_not_found_emits_terminal_event.cpp
      tests/integration/cpp/analytics/03_repository_failure_does_not_break_redirect.cpp
      tests/integration/cpp/analytics/07_queue_saturation_preserves_redirect.cpp
      tests/integration/cpp/analytics/08_graceful_shutdown_stops_worker.cpp
      tests/integration/cpp/analytics/09_disabled_mode_no_worker_required.cpp
      tests/integration/cpp/analytics/10_configured_runtime_wiring.cpp
      tests/integration/cpp/analytics/04_stats_endpoint_returns_aggregates.cpp
      tests/integration/cpp/analytics/05_stats_endpoint_time_buckets.cpp
      tests/integration/cpp/analytics/06_retention_cleanup_deletes_old_events.cpp
      tests/integration/cpp/storage/analytics_sqlite_repository_roundtrip.cpp
      tests/integration/cpp/storage/analytics_postgresql_repository_roundtrip.cpp
  )

  set(CONFIG_INTEGRATION_SOURCES
      tests/integration/config/01_inmemory_inmemory_from_yaml.cpp
      tests/integration/config/02_sqlite_none_from_yaml.cpp
      tests/integration/config/03_postgres_redis_from_yaml.cpp
      tests/integration/config/04_invalid_config_startup_failure.cpp
      tests/integration/storage/14_backend_swap_by_config_only.cpp
  )

  set(REDIS_INTEGRATION_SOURCES
      tests/integration/storage/13_rate_limit_service_integration.cpp
      tests/integration/storage/09_cache_aside_with_redis.cpp
      tests/integration/storage/10_cache_invalidation_after_update_with_redis.cpp
      tests/integration/storage/11_cache_invalidation_after_delete_with_redis.cpp
      tests/integration/storage/12_inactive_and_expired_state_not_masked_by_stale_cache.cpp
  )

  set(SECURITY_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/ControlSet.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/AccessGuard.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/PasswordHasher.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/TokenGenerator.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/AuthSession.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/UserCredentialsRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/AuthSessionRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/ServiceClient.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/ServiceClientRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/AuthAuditLogRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/FirstAdminBootstrap.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/UserManagementService.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/security/AuthBrokerService.cpp
  )

  set(SECURITY_UNIT_SOURCES
      tests/unit/security/01_control_set_test.cpp
      tests/unit/security/02_access_guard_test.cpp
      tests/unit/security/03_password_hasher_test.cpp
      tests/unit/security/04_token_generator_test.cpp
      tests/unit/security/05_first_admin_bootstrap_test.cpp
      tests/unit/security/06_user_management_service_test.cpp
      tests/unit/security/07_auth_broker_service_test.cpp
  )

  set(COMMON_CPP_SOURCES
      ${SECURITY_SOURCES}
      ${CMAKE_CURRENT_SOURCE_DIR}/src/cli_parser.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/core/utils.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/click_event_queue.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/link_repository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/http/http_server.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/http/request_handlers.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/http/http_session.cpp
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
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/postgres/PostgresSqlDialect.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/postgres/PostgresSessionFactory.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/postgres/PostgresErrorMapper.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/postgres/PostgresMigrationRunner.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/redis/RedisCacheStore.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/redis/RedisCacheSerialization.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/redis/RedisRateLimiter.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AnalyticsConfig.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AnalyticsError.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/ClickEvent.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/ClickEventSanitizer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/ClientIdHasher.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/ClickEventBuilder.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AggregateQuery.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AggregateStats.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/BoundedClickEventQueue.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AnalyticsMetrics.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AnalyticsService.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/AnalyticsWorker.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/analytics/RetentionCleanupJob.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/http/RedirectAnalyticsHook.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/http/AnalyticsStatsHandler.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/memory/InMemoryClickEventRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/storage/sql/SqlClickEventRepository.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/config/StorageConfig.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/composition/StorageFactory.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/StorageObservability.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LogLevel.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LoggingConfig.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LogFields.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/Logger.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LoggerFactory.cpp
  )

  add_library(url_shortener_common STATIC ${COMMON_CPP_SOURCES})
  target_compile_features(url_shortener_common PRIVATE cxx_std_17)
  target_include_directories(url_shortener_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(url_shortener_common
    PUBLIC
        Boost::system
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

  if(BUILD_TESTING)
  set(ALL_UNIT_SOURCES
      ${CORE_BASIC_UNIT_SOURCES}
      ${LINK_MANAGEMENT_UNIT_SOURCES}
  )

  foreach(unit_src IN LISTS ALL_UNIT_SOURCES)
    get_filename_component(unit_name ${unit_src} NAME_WE)
    add_executable(${unit_name}
        ${CMAKE_CURRENT_SOURCE_DIR}/${unit_src}
    )
    target_compile_features(${unit_name} PRIVATE cxx_std_17)
    target_compile_definitions(${unit_name} PRIVATE BOOST_TEST_DYN_LINK)
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
      target_compile_definitions(${cpp_test_target} PRIVATE BOOST_TEST_DYN_LINK)
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
  register_labeled_cpp_tests(CORE_UNIT_SOURCES           "unit")
  register_labeled_cpp_tests(INMEMORY_UNIT_SOURCES       "unit")
  register_labeled_cpp_tests(SQL_FOUNDATION_UNIT_SOURCES "unit")
  register_labeled_cpp_tests(SQLITE_UNIT_SOURCES         "unit")
  register_labeled_cpp_tests(PSQL_UNIT_SOURCES           "unit")
  register_labeled_cpp_tests(RATELIMITER_UNIT_SOURCES    "unit")
  register_labeled_cpp_tests(REDIS_UNIT_SOURCES          "unit")
  register_labeled_cpp_tests(OBSERVABILITY_UNIT_SOURCES  "unit")
  register_labeled_cpp_tests(ANALYTICS_UNIT_SOURCES      "unit")
  register_labeled_cpp_tests(ANALYTICS_INTEGRATION_SOURCES "integration")
  register_labeled_cpp_tests(CONTRACT_METADATA_SOURCES   "contract")
  register_labeled_cpp_tests(STORAGE_INTEGRATION_SOURCES "integration")
  register_labeled_cpp_tests(SQLITE_INTEGRATION_SOURCES  "integration")
  register_labeled_cpp_tests(PSQL_INTEGRATION_SOURCES    "integration")
  register_labeled_cpp_tests(REDIS_INTEGRATION_SOURCES   "integration")
  # ---- Stage 1 integration tests: 8 standalone executables (Python scripts) ----
  set(LEGACY_CORE_INTEGRATION_SOURCES
      tests/integration/core/01_generated_slug_redirect_test.py
      tests/integration/core/02_custom_slug_get_by_id_slug_test.py
      tests/integration/core/03_duplicate_slug_conflict_test.py
      tests/integration/core/04_disabled_redirect_test.py
      tests/integration/core/05_expired_redirect_test.py
      tests/integration/core/06_missing_slug_not_found_test.py
      tests/integration/core/07_permanent_temporary_status_test.py
      tests/integration/core/08_short_url_base_domain_once_test.py
  )

  set(LINK_MANAGEMENT_INTEGRATION_SOURCES
      tests/integration/link_management/01_create_with_management_fields_test.py
      tests/integration/link_management/02_disable_redirect_blocked_test.py
      tests/integration/link_management/03_enable_redirect_success_test.py
      tests/integration/link_management/04_soft_delete_management_visibility_test.py
      tests/integration/link_management/05_restore_redirect_success_test.py
      tests/integration/link_management/06_expired_redirect_blocked_test.py
      tests/integration/link_management/07_preview_status_matrix_test.py
      tests/integration/link_management/08_stats_endpoint_structure_test.py
      tests/integration/link_management/09_reserved_slug_conflict_test.py
      tests/integration/link_management/10_backward_compatibility_flow_test.py
  )

  set(BUILD_INTEGRATION_SOURCES
      tests/integration/build/01_fetch_soci_sqlite_only.py
      tests/integration/build/02_sqlite_backend_enabled.py
      tests/integration/build/03_fetch_soci_sqlite_and_postgres.py
      tests/integration/build/04_postgres_backend_enabled.py
      tests/integration/sqlite/01_db_file_initialization.py
      tests/integration/sqlite/02_busy_lock_behavior.py
      tests/integration/psql/01_connectivity_smoke.py
      tests/integration/psql/02_migration_up_down_smoke.py
      tests/integration/psql/03_connection_loss_and_retry.py
      tests/integration/psql/04_jsonb_roundtrip.py
      tests/integration/redis/01_set_get_delete_ttl.py
      tests/integration/redis/02_unavailable_redis_fail_open.py
      tests/integration/redis/03_serialization_version_compatibility.py
      tests/integration/redis/04_ratelimiter_fixed_window.py
      tests/integration/redis/05_ratelimiter_concurrency.py
      tests/integration/ci/01_inmemory_job_runner.py
      tests/integration/ci/02_sqlite_job_runner.py
      tests/integration/ci/03_postgres_job_runner.py
      tests/integration/ci/04_redis_job_runner.py
      tests/integration/ci/05_full_storage_matrix.py
      tests/integration/py/test_analytics_redirect_success_event.py
      tests/integration/py/test_analytics_redirect_404_event.py
      tests/integration/py/test_analytics_stats_schema.py
      tests/integration/py/test_analytics_stats_validation.py
      tests/integration/py/test_analytics_time_buckets.py
      tests/integration/py/test_analytics_privacy_no_raw_ip.py
      tests/integration/py/test_analytics_queue_overflow.py
      tests/integration/py/test_analytics_retention_cleanup.py
      tests/integration/py/test_analytics_disabled_mode.py
  )

  set(ALL_INTEGRATION_SOURCES
      ${LEGACY_CORE_INTEGRATION_SOURCES}
      ${LINK_MANAGEMENT_INTEGRATION_SOURCES}
      ${BUILD_INTEGRATION_SOURCES}
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
