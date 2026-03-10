# Function to fetch and build Boost
function(fetch_boost)
    message("Boost not found. Downloading and building Boost.")

    if (UNIX)
        # For UNIX-like systems (Linux and macOS)
        execute_process(
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/boost.sh
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE BOOST_BUILD_RESULT
        )
    elseif (WIN32)
        # For Windows
        execute_process(
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/boost.cmd
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE BOOST_BUILD_RESULT
        )
    else()
        message(FATAL_ERROR "Unsupported operating system.")
    endif()

    # Check the result of Boost build
    if(BOOST_BUILD_RESULT EQUAL 0)
        message("Boost build successful.")
    else()
        message(FATAL_ERROR "Boost build failed with error code: ${BOOST_BUILD_RESULT}")
    endif()

    # Set Boost_INCLUDE_DIRS and Boost_LIBRARY_DIRS explicitly
    set(Boost_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/boost-1.81/include)
    set(Boost_LIBRARY_DIRS ${CMAKE_BINARY_DIR}/boost-1.81/lib)

    # Re-try to find Boost after running boost.sh
    find_package(Boost COMPONENTS system program_options REQUIRED)
    
    # Explicitly include Boost directories
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endfunction()
