# Test configuration
#
# Each test source is a self-contained executable built against the
# header-only guchho test framework in include/test. The framework driver
# (RunAll) exposes --list-tests and --run=<Suite.Name>, which this file
# uses to register one CTest test per TEST() block.

set(GUCHHO_TEST_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")

# ------------------------------------------------------------
# test_assertions
# ------------------------------------------------------------

add_executable(test_assertions
    test_assertions.cpp
)

target_include_directories(test_assertions PRIVATE "${GUCHHO_TEST_INCLUDE_DIR}")

guchho_set_warnings(test_assertions)
guchho_enable_sanitizers(test_assertions)

# ------------------------------------------------------------
# Per-test CTest discovery
#
# A POST_BUILD step reruns "<exe> --list-tests" after the target links and
# (re)writes a tests.cmake in the build dir with one add_test() per
# "Suite.Name" line. ctest picks that file up through TEST_INCLUDE_FILES,
# so every TEST() block becomes its own ctest test driving
# "--run=<Suite.Name>".
# ------------------------------------------------------------

set(GUCHHO_TEST_LIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/test_assertions_tests.cmake")

file(
    WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/discover_tests.cmake"
    "execute_process(\n"
    "    COMMAND \"\${TEST_EXECUTABLE}\" \"--list-tests\"\n"
    "    OUTPUT_VARIABLE GUCHHO_DISCOVERY_OUT\n"
    "    RESULT_VARIABLE GUCHHO_DISCOVERY_RC\n"
    ")\n"
    "if(NOT GUCHHO_DISCOVERY_RC EQUAL 0)\n"
    "    message(FATAL_ERROR \"guchho test discovery failed\")\n"
    "endif()\n"
    "file(REMOVE \"${GUCHHO_TEST_LIST_FILE}\")\n"
    "string(REPLACE \"\\n\" \";\" GUCHHO_DISCOVERY_LINES \"\${GUCHHO_DISCOVERY_OUT}\")\n"
    "foreach(line IN LISTS GUCHHO_DISCOVERY_LINES)\n"
    "    string(STRIP \"\${line}\" GUCHHO_TEST_NAME)\n"
    "    if(NOT GUCHHO_TEST_NAME STREQUAL \"\")\n"
    "        string(REPLACE \".\" \";\" GUCHHO_TEST_PARTS \"\${GUCHHO_TEST_NAME}\")\n"
    "        list(LENGTH GUCHHO_TEST_PARTS GUCHHO_TEST_PARTS_COUNT)\n"
    "        if(GUCHHO_TEST_PARTS_COUNT EQUAL 2)\n"
    "            file(APPEND \"${GUCHHO_TEST_LIST_FILE}\"\n"
    "                \"add_test(\${GUCHHO_TEST_NAME} \"\n"
    "                \"\\\"\${TEST_EXECUTABLE}\\\" \"\n"
    "                \"\\\"--run=\${GUCHHO_TEST_NAME}\\\")\\n\")\n"
    "        endif()\n"
    "    endif()\n"
    "endforeach()\n"
)

add_custom_command(
    TARGET test_assertions
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXECUTABLE=$<TARGET_FILE:test_assertions>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/discover_tests.cmake"
)

set_property(
    DIRECTORY
    APPEND
    PROPERTY TEST_INCLUDE_FILES "${GUCHHO_TEST_LIST_FILE}"
)