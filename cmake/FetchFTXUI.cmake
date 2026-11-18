# ---------------------------------------------------------------------------
# FetchFTXUI.cmake - make the FTXUI terminal-UI libraries available.
#
# Mirrors the FetchContent pattern of FetchSOCI.cmake / the yaml-cpp and
# hiredis blocks in CMakeLists.txt: prefer a package-manager-provided copy
# (the vcpkg `tui` feature), otherwise fetch the pinned upstream.
#
# Supply-chain pin (ADR 0003 Decision 1, threat model T-122): FTXUI is pinned
# by an *immutable commit SHA*, never a movable Git tag or branch. A tag can be
# re-pointed upstream and GIT_SHALLOW would then fetch whatever it references
# (CWE-494 / CWE-829). GIT_SHALLOW TRUE is safe here only because GIT_TAG is a
# full 40-hex SHA that is reachable from a published tag, so GitHub's
# allowReachableSHA1InWant lets a depth-1 fetch resolve it.
#
#   Pinned:   v7.0.3
#   Commit:   f921fad208912747c17d129a8ef75ec7624b6eec
#   Verified: 2026-11-18 (tag commit dated 2026-08-06, GPG-signed; builds the
#             component/dom/screen libraries as cxx_std_17 per
#             cmake/ftxui_set_options.cmake at this SHA)
# ---------------------------------------------------------------------------

include(FetchContent)

# fetch_ftxui() : expose the ftxui::screen / ftxui::dom / ftxui::component
# imported targets. Call once, only when URLSHORTENER_ENABLE_TUI is ON.
function(fetch_ftxui)
  # Prefer a pre-built FTXUI (vcpkg `tui` feature / system install) only when it
  # matches the audited release. Requesting the version makes find_package reject
  # a different FTXUI so a stale/older system copy cannot silently shadow the
  # pinned SHA below (security review follow-up). FTXUI's config version file is
  # generated with COMPATIBILITY SameMajorVersion, so this accepts 7.x >= 7.0.3
  # and rejects anything older or a different major (e.g. the vcpkg baseline's
  # 6.1.9), falling through to the SHA-pinned FetchContent instead.
  find_package(ftxui 7.0.3 CONFIG QUIET)
  if(ftxui_FOUND)
    message(STATUS "FTXUI: using find_package (version ${ftxui_VERSION})")
    return()
  endif()

  # Immutable commit SHA for FTXUI v7.0.3 (see header comment, T-122).
  set(URLSHORTENER_FTXUI_GIT_TAG
      "f921fad208912747c17d129a8ef75ec7624b6eec"
      CACHE STRING "Pinned FTXUI commit SHA (release v7.0.3)")

  # Build only the libraries: no examples, docs, tests, fuzzer, C++20 modules
  # or install target may pollute our build tree or CTest. Option names are the
  # FTXUI v7.0.3 names (verified against its CMakeLists.txt at the pinned SHA).
  set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_TESTS_FUZZER OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_MODULES OFF CACHE BOOL "" FORCE)
  set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(FTXUI_QUIET ON CACHE BOOL "" FORCE)

  message(STATUS
      "FTXUI: fetching ${URLSHORTENER_FTXUI_GIT_TAG} (v7.0.3) via FetchContent")

  # SYSTEM (CMake 3.25+, matching the project's cmake_minimum_required) marks
  # FTXUI's interface include dirs as system headers for our targets, so its
  # headers raise no warnings in url_shortener_tui / the render tests.
  # GIT_SUBMODULES "" fetches no submodules (FTXUI needs none for the libraries;
  # mirrors cmake/FetchSOCI.cmake) - smaller, more deterministic checkout.
  FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG ${URLSHORTENER_FTXUI_GIT_TAG}
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SYSTEM
  )
  FetchContent_MakeAvailable(ftxui)
endfunction()
