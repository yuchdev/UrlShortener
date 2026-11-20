# compare_generated_doc.cmake
#
# Authoritative drift check for the generated API reference: runs the REAL
# generator (tools/route_registry_dump) and asserts the committed
# docs/api/README.md is byte-for-byte its output, after CRLF normalization.
#
# This complements the in-process docs__05 renderer. docs__05 duplicates the
# generator's ~40-line renderer for a fast unit check, but if the real tool's
# output format ever changes, docs__05 can still pass against a stale doc. This
# script runs the tool itself, so it cannot.
#
# Required -D variables (all absolute paths):
#   TOOL - the built route_registry_dump executable
#   DOC  - the committed docs/api/README.md
#   OUT  - where to write the freshly generated reference

if(NOT TOOL)
    message(FATAL_ERROR "compare_generated_doc.cmake: TOOL not set")
endif()
if(NOT DOC)
    message(FATAL_ERROR "compare_generated_doc.cmake: DOC not set")
endif()
if(NOT OUT)
    message(FATAL_ERROR "compare_generated_doc.cmake: OUT not set")
endif()

execute_process(
    COMMAND "${TOOL}"
    OUTPUT_FILE "${OUT}"
    RESULT_VARIABLE tool_rc
    TIMEOUT 60
)
if(NOT tool_rc EQUAL 0)
    message(FATAL_ERROR
        "route_registry_dump failed (result: ${tool_rc}); "
        "cannot verify docs/api/README.md")
endif()

if(NOT EXISTS "${DOC}")
    message(FATAL_ERROR "committed API reference not found: ${DOC}")
endif()

file(READ "${OUT}" generated)
file(READ "${DOC}" committed)
string(REPLACE "\r\n" "\n" generated "${generated}")
string(REPLACE "\r\n" "\n" committed "${committed}")

if(NOT generated STREQUAL committed)
    message(FATAL_ERROR
        "docs/api/README.md is out of date with the route registry; "
        "regenerate: cmake-build/route_registry_dump > docs/api/README.md")
endif()
