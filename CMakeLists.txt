cmake_minimum_required(VERSION 3.25)

include(cmake/prelude.cmake)

project(
    simple-http
    VERSION 1.0.0
    DESCRIPTION "Simple HTTP Server"
    HOMEPAGE_URL "https://github.com/yuchdev/interview-linux"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)
include(cmake/fetch-boost.cmake)

# ---- Check dependencies ----
set(CMAKE_CXX_STANDARD 17)

# Set Boost_DIR explicitly
set(Boost_DIR ${CMAKE_BINARY_DIR}/boost-1.81.0/boost-1.81/lib/cmake/Boost-1.81.0)

# Set the paths where CMake should look for Boost libraries
set(Boost_LIBRARY_DIRS
    ${CMAKE_BINARY_DIR}/boost-1.81.0/boost-1.81/lib
)

# Set the paths where CMake should look for Boost headers
set(Boost_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}/boost-1.81.0/boost-1.81/include
)

# Explicitly include Boost directories
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})

# Try to find Boost before running the boost.sh script
find_package(Boost COMPONENTS system program_options
    PATHS
    ${Boost_LIBRARY_DIRS}  # Specify additional paths to search for libraries
    NO_DEFAULT_PATH  # Do not search in default system paths
)

# If Boost is not found, run the boost.sh script
if(NOT Boost_FOUND)
    fetch_boost()
endif()


find_package(OpenSSL REQUIRED)

# ---- Declare executable ----
# Load HTTP_SERVER_SOURCES
include(sources.cmake)

add_executable(simple-http ${HTTP_SERVER_SOURCES})
add_executable(simple-http::exe ALIAS simple-http)

set_property(TARGET simple-http PROPERTY OUTPUT_NAME simple-http)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

target_compile_features(simple-http PRIVATE cxx_std_17)

target_include_directories(simple-http
        PRIVATE
            ${Boost_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(simple-http
        PRIVATE
        ${Boost_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

# ---- Install rules ----
if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----
if(NOT simple-http_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of simple-http"
  )
endif()

include(cmake/dev-mode.cmake)
include(CTest)
enable_testing()
