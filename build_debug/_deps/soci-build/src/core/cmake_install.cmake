# Install script for directory: /tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/src/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soci" TYPE FILE FILES
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/backend-loader.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/bind-values.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/blob-exchange.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/blob.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/boost-fusion.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/boost-gregorian-date.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/boost-optional.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/boost-tuple.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/callbacks.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/column-info.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/connection-parameters.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/connection-pool.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/error.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/exchange-traits.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/into-type.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/into.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/logger.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/noreturn.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/once-temp-type.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/prepare-temp-type.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/procedure.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/query_transformation.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/ref-counted-prepare-info.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/ref-counted-statement.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/row-exchange.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/row.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/rowid-exchange.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/rowid.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/rowset.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/session.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/soci-backend.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/soci-platform.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/soci-simple.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/soci.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/statement.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/transaction.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/type-conversion-traits.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/type-conversion.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/type-holder.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/type-ptr.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/type-wrappers.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/unsigned-types.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/use-type.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/use.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/values-exchange.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/values.h"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src/include/soci/version.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/tmp/workspace/yuchdev/UrlShortener/build_debug/lib/libsoci_core.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI/SOCITargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI/SOCITargets.cmake"
         "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/CMakeFiles/Export/39447b8616a7e0144dc6bdda034be1e2/SOCITargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI/SOCITargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI/SOCITargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI" TYPE FILE FILES "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/CMakeFiles/Export/39447b8616a7e0144dc6bdda034be1e2/SOCITargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI" TYPE FILE FILES "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/CMakeFiles/Export/39447b8616a7e0144dc6bdda034be1e2/SOCITargets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/SOCI" TYPE FILE FILES
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/SOCIConfig.cmake"
    "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/SOCIConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build/src/core/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
