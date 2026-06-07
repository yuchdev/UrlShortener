#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SOCI::soci_core_static" for configuration "Debug"
set_property(TARGET SOCI::soci_core_static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SOCI::soci_core_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LINK_INTERFACE_LIBRARIES_DEBUG "/usr/lib/x86_64-linux-gnu/libdl.a;Boost::date_time"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libsoci_core.a"
  )

list(APPEND _cmake_import_check_targets SOCI::soci_core_static )
list(APPEND _cmake_import_check_files_for_SOCI::soci_core_static "${_IMPORT_PREFIX}/lib/libsoci_core.a" )

# Import target "SOCI::soci_empty_static" for configuration "Debug"
set_property(TARGET SOCI::soci_empty_static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SOCI::soci_empty_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libsoci_empty.a"
  )

list(APPEND _cmake_import_check_targets SOCI::soci_empty_static )
list(APPEND _cmake_import_check_files_for_SOCI::soci_empty_static "${_IMPORT_PREFIX}/lib/libsoci_empty.a" )

# Import target "SOCI::soci_postgresql_static" for configuration "Debug"
set_property(TARGET SOCI::soci_postgresql_static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SOCI::soci_postgresql_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LINK_INTERFACE_LIBRARIES_DEBUG "/usr/lib/x86_64-linux-gnu/libpq.so;SOCI::soci_core_static"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libsoci_postgresql.a"
  )

list(APPEND _cmake_import_check_targets SOCI::soci_postgresql_static )
list(APPEND _cmake_import_check_files_for_SOCI::soci_postgresql_static "${_IMPORT_PREFIX}/lib/libsoci_postgresql.a" )

# Import target "SOCI::soci_sqlite3_static" for configuration "Debug"
set_property(TARGET SOCI::soci_sqlite3_static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SOCI::soci_sqlite3_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LINK_INTERFACE_LIBRARIES_DEBUG "/usr/lib/x86_64-linux-gnu/libsqlite3.so;/usr/lib/x86_64-linux-gnu/libsqlite3.so;SOCI::soci_core_static"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libsoci_sqlite3.a"
  )

list(APPEND _cmake_import_check_targets SOCI::soci_sqlite3_static )
list(APPEND _cmake_import_check_files_for_SOCI::soci_sqlite3_static "${_IMPORT_PREFIX}/lib/libsoci_sqlite3.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
