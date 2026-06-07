# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-src")
  file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-src")
endif()
file(MAKE_DIRECTORY
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-build"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/tmp"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/src/hiredis-populate-stamp"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/src"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/src/hiredis-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/src/hiredis-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/hiredis-subbuild/hiredis-populate-prefix/src/hiredis-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
