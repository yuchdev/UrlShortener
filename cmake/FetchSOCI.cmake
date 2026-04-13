include(FetchContent)

function(fetch_soci_sqlite_only)
  set(SOCI_TESTS OFF CACHE BOOL "" FORCE)
  set(SOCI_STATIC ON CACHE BOOL "" FORCE)
  set(SOCI_SHARED OFF CACHE BOOL "" FORCE)
  set(SOCI_DB2 OFF CACHE BOOL "" FORCE)
  set(SOCI_FIREBIRD OFF CACHE BOOL "" FORCE)
  set(SOCI_MYSQL OFF CACHE BOOL "" FORCE)
  set(SOCI_ODBC OFF CACHE BOOL "" FORCE)
  set(SOCI_ORACLE OFF CACHE BOOL "" FORCE)
  set(SOCI_POSTGRESQL OFF CACHE BOOL "" FORCE)
  set(SOCI_SQLITE3 ON CACHE BOOL "" FORCE)
  set(WITH_DB2 OFF CACHE BOOL "" FORCE)
  set(WITH_FIREBIRD OFF CACHE BOOL "" FORCE)
  set(WITH_MYSQL OFF CACHE BOOL "" FORCE)
  set(WITH_ODBC OFF CACHE BOOL "" FORCE)
  set(WITH_ORACLE OFF CACHE BOOL "" FORCE)
  set(WITH_POSTGRESQL OFF CACHE BOOL "" FORCE)
  set(WITH_SQLITE3 ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    soci
    GIT_REPOSITORY https://github.com/SOCI/soci.git
    GIT_TAG v4.0.3
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable(soci)

  # SOCI v4.0.3 creates shared targets (soci_core, soci_sqlite3) when SOCI_SHARED=ON,
  # and static targets (soci_core_static, soci_sqlite3_static) when SOCI_STATIC=ON.
  # Check for both variants so the aliases work regardless of which was built.
  #
  # The static targets don't propagate the build directory's include path
  # (where soci-config.h is generated), so add it explicitly.
  if(TARGET soci_core_static)
    target_include_directories(soci_core_static PUBLIC
        $<BUILD_INTERFACE:${soci_BINARY_DIR}/include>)
    add_library(SOCI::soci_core ALIAS soci_core_static)
  elseif(TARGET soci_core)
    add_library(SOCI::soci_core ALIAS soci_core)
  endif()
  if(TARGET soci_sqlite3_static)
    target_include_directories(soci_sqlite3_static PUBLIC
        $<BUILD_INTERFACE:${soci_BINARY_DIR}/include>)
    # Make the SQLite3 static backend explicitly depend on the core static
    # library so downstream link order is correct. Use plain (non-keyword)
    # signature to match what SOCI's own CMakeLists.txt already uses.
    if(TARGET soci_core_static)
      target_link_libraries(soci_sqlite3_static soci_core_static)
    endif()
    add_library(SOCI::soci_sqlite3 ALIAS soci_sqlite3_static)
  elseif(TARGET soci_sqlite3)
    add_library(SOCI::soci_sqlite3 ALIAS soci_sqlite3)
  endif()

  if(NOT TARGET SOCI::soci_core)
    message(FATAL_ERROR "SOCI core target is missing. Ensure SOCI fetch/configure succeeded.")
  endif()
  if(NOT TARGET SOCI::soci_sqlite3)
    message(FATAL_ERROR "SOCI SQLite3 backend target is missing. Enable SQLite backend for this stage.")
  endif()

  if(TARGET soci_postgresql OR TARGET SOCI::soci_postgresql)
    message(FATAL_ERROR "PostgreSQL backend must remain disabled in this subtask.")
  endif()
endfunction()
