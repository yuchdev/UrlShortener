# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src")
  file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-src")
endif()
file(MAKE_DIRECTORY
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-build"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/tmp"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/src/soci-populate-stamp"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/src"
  "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/src/soci-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/src/soci-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/tmp/workspace/yuchdev/UrlShortener/build_debug/_deps/soci-subbuild/soci-populate-prefix/src/soci-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
