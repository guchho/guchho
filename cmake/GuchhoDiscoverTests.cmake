# ============================================================
# Guchho dynamic CTest discovery
#
# Executed with:
#
#   cmake -P <this-file>
#
# Usually invoked from a POST_BUILD step.
#
# The test executable is expected to support:
#
#   --list-tests
#   --run=<test-name>
#
# This script discovers all tests and generates a CMake file
# containing one add_test() entry per discovered TEST().
# ============================================================


# ------------------------------------------------------------
# Required arguments
# ------------------------------------------------------------

if(NOT DEFINED TEST_EXECUTABLE OR TEST_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR
        "TEST_EXECUTABLE is required"
    )
endif()


if(NOT DEFINED TEST_OUTPUT OR TEST_OUTPUT STREQUAL "")
    message(FATAL_ERROR
        "TEST_OUTPUT is required"
    )
endif()


# ------------------------------------------------------------
# Optional arguments
# ------------------------------------------------------------

if(NOT DEFINED TEST_PREFIX)
    set(TEST_PREFIX "")
endif()


if(NOT DEFINED TEST_WORKING_DIR OR TEST_WORKING_DIR STREQUAL "")
    set(TEST_WORKING_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()


# ------------------------------------------------------------
# Normalize paths
# ------------------------------------------------------------

get_filename_component(
    TEST_EXECUTABLE
    "${TEST_EXECUTABLE}"
    ABSOLUTE
)

get_filename_component(
    TEST_OUTPUT_DIR
    "${TEST_OUTPUT}"
    DIRECTORY
)


# ------------------------------------------------------------
# Make sure the output directory exists
# ------------------------------------------------------------

if(NOT TEST_OUTPUT_DIR STREQUAL "")
    file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
endif()


# ------------------------------------------------------------
# Validate test executable
# ------------------------------------------------------------

if(NOT EXISTS "${TEST_EXECUTABLE}")
    message(WARNING
        "Test executable does not exist: ${TEST_EXECUTABLE}"
    )

    file(
        WRITE
        "${TEST_OUTPUT}"
        "add_test("
        "[=[${TEST_PREFIX}discovery_failed]=] "
        "[==[${TEST_EXECUTABLE}]==] "
        "--list-tests"
        ")\n"
        "set_tests_properties("
        "[=[${TEST_PREFIX}discovery_failed]=] "
        "PROPERTIES "
        "WORKING_DIRECTORY [==[${TEST_WORKING_DIR}]==]"
        ")\n"
    )

    return()
endif()


# ------------------------------------------------------------
# Discover tests
#
# Expected output:
#
#   test_name_1
#   test_name_2
#   test_name_3
#
# One test name per line.
# ------------------------------------------------------------

execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-tests

    WORKING_DIRECTORY "${TEST_WORKING_DIR}"

    OUTPUT_VARIABLE discovery_output
    ERROR_VARIABLE discovery_error

    RESULT_VARIABLE discovery_result

    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)


# ------------------------------------------------------------
# Discovery failed
# ------------------------------------------------------------

if(NOT discovery_result EQUAL 0)

    file(
        WRITE
        "${TEST_OUTPUT}"

        "add_test("
        "[=[${TEST_PREFIX}discovery_failed]=] "
        "[==[${TEST_EXECUTABLE}]==] "
        "--list-tests"
        ")\n"

        "set_tests_properties("
        "[=[${TEST_PREFIX}discovery_failed]=] "
        "PROPERTIES "
        "WORKING_DIRECTORY [==[${TEST_WORKING_DIR}]==]"
        ")\n"
    )

    message(
        WARNING
        "Test discovery failed for ${TEST_EXECUTABLE}"
    )

    if(NOT discovery_error STREQUAL "")
        message(
            WARNING
            "Discovery error:\n${discovery_error}"
        )
    endif()

    return()

endif()


# ------------------------------------------------------------
# Normalize Windows CRLF output
# ------------------------------------------------------------

string(
    REPLACE "\r\n" "\n"
    discovery_output
    "${discovery_output}"
)


string(
    REPLACE "\r" ""
    discovery_output
    "${discovery_output}"
)


# ------------------------------------------------------------
# Convert output into a CMake list
# ------------------------------------------------------------

string(
    REPLACE "\n" ";"
    discovery_lines
    "${discovery_output}"
)


# ------------------------------------------------------------
# Generate CTest definitions
# ------------------------------------------------------------

set(content "")

set(discovered_count 0)

set(seen_tests "")


foreach(line IN LISTS discovery_lines)

    # Remove leading/trailing whitespace.
    string(STRIP "${line}" line)


    # Ignore empty lines.
    if(line STREQUAL "")
        continue()
    endif()


    # --------------------------------------------------------
    # Prevent duplicate test names
    # --------------------------------------------------------

    list(FIND seen_tests "${line}" existing_index)

    if(NOT existing_index EQUAL -1)
        message(
            WARNING
            "Duplicate test discovered: ${line}"
        )

        continue()
    endif()


    list(APPEND seen_tests "${line}")


    # --------------------------------------------------------
    # Full CTest name
    # --------------------------------------------------------

    set(test_name "${TEST_PREFIX}${line}")


    # --------------------------------------------------------
    # Generate add_test()
    #
    # CMake bracket arguments are used so test names and paths
    # containing spaces or special characters remain safe.
    # --------------------------------------------------------

    string(
        APPEND
        content

        "add_test("
        "[=[${test_name}]=] "
        "[==[${TEST_EXECUTABLE}]==] "
        "[==[--run=${line}]==]"
        ")\n"

        "set_tests_properties("
        "[=[${test_name}]=] "
        "PROPERTIES "
        "WORKING_DIRECTORY [==[${TEST_WORKING_DIR}]==]"
        ")\n"
    )


    math(EXPR discovered_count "${discovered_count} + 1")

endforeach()


# ------------------------------------------------------------
# No tests discovered
# ------------------------------------------------------------

if(discovered_count EQUAL 0)

    # Do not create an empty CTest file.
    #
    # The parent CMake logic can detect the missing file and
    # install its NOT_BUILT / placeholder test.
    #

    if(EXISTS "${TEST_OUTPUT}")
        file(REMOVE "${TEST_OUTPUT}")
    endif()

    message(
        STATUS
        "No tests discovered for ${TEST_EXECUTABLE}"
    )

    return()

endif()


# ------------------------------------------------------------
# Write generated CMake file
# ------------------------------------------------------------

file(
    WRITE
    "${TEST_OUTPUT}"
    "${content}"
)


# ------------------------------------------------------------
# Summary
# ------------------------------------------------------------

message(
    STATUS
    "Discovered ${discovered_count} test(s) from ${TEST_EXECUTABLE}"
)

message(
    STATUS
    "Generated CTest file: ${TEST_OUTPUT}"
)