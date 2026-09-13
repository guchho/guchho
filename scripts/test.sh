#!/usr/bin/env bash

set -uo pipefail

# ========================================
# Guchho Test System
# ========================================

PRESET="release-win32-x64"
TEST_FILTER=""
TEST_TIMEOUT=""

VERBOSE=false
ALL_ERRORS=false
NO_STOP=false
TEST_VERBOSE=false

LOG_FILE=""

# ========================================
# Colors
# ========================================

if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    BLUE='\033[0;34m'
    RESET='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    CYAN=''
    BLUE=''
    RESET=''
fi

# ========================================
# Logging helper
# ========================================

run_command() {
    if [[ -n "${LOG_FILE}" ]]; then
        "$@" 2>&1 | tee -a "${LOG_FILE}"
        return "${PIPESTATUS[0]}"
    else
        "$@"
    fi
}

# ========================================
# Argument parsing
# ========================================

while [[ $# -gt 0 ]]; do

    case "$1" in

        # --------------------------------
        # CMake preset
        # --------------------------------

        --preset)
            if [[ $# -lt 2 ]]; then
                echo "Error: --preset requires a value."
                exit 1
            fi

            PRESET="$2"
            shift 2
            ;;

        # --------------------------------
        # Test filter
        # --------------------------------

        --filter)
            if [[ $# -lt 2 ]]; then
                echo "Error: --filter requires a value."
                exit 1
            fi

            TEST_FILTER="$2"
            shift 2
            ;;

        # --------------------------------
        # Test timeout
        # --------------------------------

        --timeout)
            if [[ $# -lt 2 ]]; then
                echo "Error: --timeout requires a value."
                exit 1
            fi

            TEST_TIMEOUT="$2"
            shift 2
            ;;

        # --------------------------------
        # Log file
        # --------------------------------

        --log)
            if [[ $# -lt 2 ]]; then
                echo "Error: --log requires a file."
                exit 1
            fi

            LOG_FILE="$2"
            shift 2
            ;;

        # --------------------------------
        # Verbose
        # --------------------------------

        --verbose|-v)
            VERBOSE=true
            TEST_VERBOSE=true
            shift
            ;;

        # --------------------------------
        # ALL ERRORS
        # --------------------------------

        --all-errors)
            ALL_ERRORS=true
            NO_STOP=true
            TEST_VERBOSE=true
            VERBOSE=true
            shift
            ;;

        # --------------------------------
        # Do not stop tests
        # --------------------------------

        --no-stop)
            NO_STOP=true
            shift
            ;;

        # --------------------------------
        # Verbose tests
        # --------------------------------

        --test-verbose)
            TEST_VERBOSE=true
            shift
            ;;

        # --------------------------------
        # Help
        # --------------------------------

        -h|--help)

            cat <<'EOF'

========================================
Guchho Test System
========================================

Usage:

  bash scripts/test.sh [options]


Test options:

  --preset <preset>
      Select CMake preset

  --filter <regex>
      Run tests matching regex

  --timeout <seconds>
      Per-test timeout


Error / diagnostic options:

  --all-errors
      Show ALL test errors.
      Enables:
        --no-stop
        --test-verbose

  --no-stop
      Do not stop CTest after the first failure

  --test-verbose
      Show full CTest output


Output:

  --log <file>
      Save complete test output to file

  --verbose, -v
      Enable verbose test output


Help:

  -h, --help
      Show this help


Examples:

  # Run all tests
  bash scripts/test.sh

  # Run all Assertions tests
  bash scripts/test.sh \
      --filter "Assertions.*"

  # Run a single test
  bash scripts/test.sh \
      --filter "Assertions.TestName" \
      --all-errors

  # Verbose + log
  bash scripts/test.sh \
      --test-verbose \
      --log test.log

EOF

            exit 0
            ;;

        # --------------------------------
        # Unknown option
        # --------------------------------

        *)
            echo
            echo "Error: Unknown option: $1"
            echo
            echo "Run:"
            echo "  bash scripts/test.sh --help"
            echo
            exit 1
            ;;

    esac

done

# ========================================
# Paths
# ========================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build/${PRESET}"

# ========================================
# Prepare log
# ========================================

if [[ -n "${LOG_FILE}" ]]; then

    if [[ "${LOG_FILE}" != /* ]]; then
        LOG_FILE="${ROOT_DIR}/${LOG_FILE}"
    fi

    mkdir -p "$(dirname "${LOG_FILE}")"

    : > "${LOG_FILE}"

fi

# ========================================
# Header
# ========================================

echo
echo "========================================"
echo " G U C H H O"
echo " Test System"
echo "========================================"

echo
echo "Preset       : ${PRESET}"
echo "Build Dir    : ${BUILD_DIR}"

if [[ -n "${TEST_FILTER}" ]]; then
    echo "Test Filter  : ${TEST_FILTER}"
else
    echo "Test Filter  : ALL"
fi

if [[ -n "${TEST_TIMEOUT}" ]]; then
    echo "Test Timeout : ${TEST_TIMEOUT}s"
else
    echo "Test Timeout : none"
fi

echo "Verbose      : ${VERBOSE}"
echo "All Errors   : ${ALL_ERRORS}"
echo "No Stop      : ${NO_STOP}"
echo "Test Verbose : ${TEST_VERBOSE}"

if [[ -n "${LOG_FILE}" ]]; then
    echo "Log File     : ${LOG_FILE}"
else
    echo "Log File     : none"
fi

echo

# ========================================
# Check required tools
# ========================================

echo "========================================"
echo "Checking Required Tools"
echo "========================================"

if ! command -v ctest >/dev/null 2>&1; then
    echo
    echo -e "${RED}[ERROR] CTest was not found.${RESET}"
    echo
    exit 1
fi

echo
echo -e "${GREEN}[OK] CTest:${RESET}"
ctest --version | head -n 1

echo

# ========================================
# Check build directory
# ========================================

echo "========================================"
echo "Checking Build Directory"
echo "========================================"
echo

if [[ ! -f "${BUILD_DIR}/CTestTestfile.cmake" ]]; then

    echo -e "${RED}[ERROR] No CTest configuration found:${RESET}"
    echo "  ${BUILD_DIR}/CTestTestfile.cmake"
    echo
    echo "Build the project first:"
    echo
    echo "  bash scripts/build.sh"
    echo
    echo "or select another preset:"
    echo
    echo "  bash scripts/test.sh --preset <preset>"
    echo
    exit 1

fi

echo -e "${GREEN}[OK] Build directory found.${RESET}"
echo

# ========================================
# Status
# ========================================

TEST_STATUS=0

# ========================================
# Run tests
# ========================================

echo "========================================"
echo "RUNNING TESTS"
echo "========================================"
echo

CTEST_CMD=(
    ctest
    --preset
    "${PRESET}"
    --output-on-failure
)

# ========================================
# IMPORTANT:
# Only stop after first failure when
# --no-stop / --all-errors is NOT used.
# ========================================

if [[ "${NO_STOP}" != true &&
      "${ALL_ERRORS}" != true ]]; then

    CTEST_CMD+=(
        --stop-on-failure
    )

fi

# ========================================
# Test filter
# ========================================

if [[ -n "${TEST_FILTER}" ]]; then

    CTEST_CMD+=(
        -R
        "${TEST_FILTER}"
    )

fi

# ========================================
# Timeout
# ========================================

if [[ -n "${TEST_TIMEOUT}" ]]; then

    CTEST_CMD+=(
        --timeout
        "${TEST_TIMEOUT}"
    )

fi

# ========================================
# Verbose CTest
# ========================================

if [[ "${TEST_VERBOSE}" == true ||
      "${ALL_ERRORS}" == true ||
      "${VERBOSE}" == true ]]; then

    CTEST_CMD+=(
        -VV
    )

fi

echo -e "${BLUE}>${RESET} ${CTEST_CMD[*]}"
echo

run_command "${CTEST_CMD[@]}"

TEST_STATUS=$?

echo

if [[ ${TEST_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[OK] Tests completed successfully.${RESET}"

else

    echo -e "${RED}[ERROR] Tests failed.${RESET}"
    echo "Exit code: ${TEST_STATUS}"

fi

echo

# ========================================
# Final result
# ========================================

echo "========================================"
echo " G U C H H O"
echo " FINAL RESULT"
echo "========================================"
echo

if [[ ${TEST_STATUS} -eq 0 ]]; then

    echo "========================================"
    echo -e "${GREEN} TESTS COMPLETED SUCCESSFULLY ${RESET}"
    echo "========================================"
    echo

    if [[ -n "${LOG_FILE}" ]]; then
        echo "Log:"
        echo "  ${LOG_FILE}"
        echo
    fi

    exit 0

fi

# ========================================
# Failure summary
# ========================================

echo "========================================"
echo -e "${RED} TESTS COMPLETED WITH ERRORS ${RESET}"
echo "========================================"

echo
echo -e "${RED}[FAIL] Tests${RESET}"
echo

if [[ -n "${LOG_FILE}" ]]; then

    echo "Complete output saved to:"
    echo "  ${LOG_FILE}"

else

    echo "For a complete diagnostic log run:"
    echo
    echo "  bash scripts/test.sh --all-errors --log test-errors.log"

fi

echo

exit 1