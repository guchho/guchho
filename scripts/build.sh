#!/usr/bin/env bash

set -uo pipefail

# ========================================
# Guchho Build System
# ========================================

PRESET="release-win32-x64"
TARGET=""
TEST_FILTER="bundler"
TEST_FILTER_EXPLICIT=false
TEST_TIMEOUT=""

VERBOSE=false
ALL_ERRORS=false
NO_STOP=false
BUILD_VERBOSE=false
TEST_VERBOSE=false

LOG_FILE=""

# ========================================
# Detect CPU jobs automatically
# ========================================

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=4
fi

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
        # Build target
        # --------------------------------

        --target)
            if [[ $# -lt 2 ]]; then
                echo "Error: --target requires a value."
                exit 1
            fi

            TARGET="$2"
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
            TEST_FILTER_EXPLICIT=true
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
            BUILD_VERBOSE=true
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
        # Verbose build
        # --------------------------------

        --build-verbose)
            BUILD_VERBOSE=true
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
Guchho Build System
========================================

Usage:

  bash scripts/build.sh [options]


Build options:

  --preset <preset>
      Select CMake preset

  --target <target>
      Build specific target


Test options:

  --filter <regex>
      Run tests matching regex

  --timeout <seconds>
      Per-test timeout


Error / diagnostic options:

  --all-errors
      Show ALL available build and test errors.
      Enables:
        --no-stop
        --build-verbose
        --test-verbose

  --no-stop
      Do not stop CTest after the first failure

  --build-verbose
      Show full compiler and linker commands

  --test-verbose
      Show full CTest output


Output:

  --log <file>
      Save complete build/test output to file

  --verbose, -v
      Enable verbose test output


Help:

  -h, --help
      Show this help


Examples:

  # Normal build
  bash scripts/build.sh

  # Show ALL errors
  bash scripts/build.sh --all-errors

  # ALL errors + log
  bash scripts/build.sh --all-errors --log errors.log

  # Specific test
  bash scripts/build.sh \
      --filter "BundlerDefault.TestMetafileNoBundle" \
      --all-errors

  # All Bundler tests
  bash scripts/build.sh \
      --filter "Bundler.*" \
      --all-errors

  # Specific target
  bash scripts/build.sh \
      --target guchho_bundler_tests \
      --all-errors

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
            echo "  bash scripts/build.sh --help"
            echo
            exit 1
            ;;

    esac

done

# ========================================
# Auto-derive test filter from target
# ========================================

if [[ -n "${TARGET}" && "${TEST_FILTER_EXPLICIT}" != true ]]; then

    DERIVED_FILTER="${TARGET}"

    # Strip leading "guchho_" prefix
    if [[ "${DERIVED_FILTER}" == guchho_* ]]; then
        DERIVED_FILTER="${DERIVED_FILTER#guchho_}"
    fi

    # Strip trailing "_tests" suffix
    if [[ "${DERIVED_FILTER}" == *_tests ]]; then
        DERIVED_FILTER="${DERIVED_FILTER%_tests}"
    fi

    # Only override the filter if a meaningful name remains
    if [[ -n "${DERIVED_FILTER}" ]]; then
        TEST_FILTER="${DERIVED_FILTER}"
    fi

fi

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
echo " Build System"
echo "========================================"

echo
echo "Preset       : ${PRESET}"
echo "Build Dir    : ${BUILD_DIR}"

if [[ -n "${TARGET}" ]]; then
    echo "Target       : ${TARGET}"
else
    echo "Target       : ALL"
fi

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

echo "Jobs         : ${JOBS}"
echo "Verbose      : ${VERBOSE}"
echo "All Errors   : ${ALL_ERRORS}"
echo "No Stop      : ${NO_STOP}"
echo "Build Verbose: ${BUILD_VERBOSE}"
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

if ! command -v cmake >/dev/null 2>&1; then
    echo
    echo -e "${RED}[ERROR] CMake was not found.${RESET}"
    echo
    exit 1
fi

if ! command -v ctest >/dev/null 2>&1; then
    echo
    echo -e "${RED}[ERROR] CTest was not found.${RESET}"
    echo
    exit 1
fi

echo
echo -e "${GREEN}[OK] CMake:${RESET}"
cmake --version | head -n 1

echo -e "${GREEN}[OK] CTest:${RESET}"
ctest --version | head -n 1

echo

# ========================================
# Status
# ========================================

CONFIGURE_STATUS=0
BUILD_STATUS=0
TEST_STATUS=0

# ========================================
# 1. Configure
# ========================================

echo
echo "========================================"
echo "[1/3] CONFIGURING"
echo "========================================"
echo

CONFIGURE_CMD=(
    cmake
    --preset
    "${PRESET}"
)

echo -e "${BLUE}>${RESET} ${CONFIGURE_CMD[*]}"
echo

run_command "${CONFIGURE_CMD[@]}"

CONFIGURE_STATUS=$?

echo

if [[ ${CONFIGURE_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[OK] CMake configuration completed.${RESET}"

else

    echo -e "${RED}[ERROR] CMake configuration failed.${RESET}"
    echo "Exit code: ${CONFIGURE_STATUS}"

    echo
    echo "Continuing to collect remaining errors..."

fi

# ========================================
# 2. Build
# ========================================

echo
echo "========================================"
echo "[2/3] BUILDING"
echo "========================================"
echo

BUILD_CMD=(
    cmake
    --build
    --preset
    "${PRESET}"
    --parallel
    "${JOBS}"
)

if [[ -n "${TARGET}" ]]; then

    BUILD_CMD+=(
        --target
        "${TARGET}"
    )

fi

# ========================================
# Verbose compiler output
# ========================================

if [[ "${BUILD_VERBOSE}" == true ||
      "${ALL_ERRORS}" == true ||
      "${VERBOSE}" == true ]]; then

    BUILD_CMD+=(
        --verbose
    )

fi

echo -e "${BLUE}>${RESET} ${BUILD_CMD[*]}"
echo

run_command "${BUILD_CMD[@]}"

BUILD_STATUS=$?

echo

if [[ ${BUILD_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[OK] Build completed.${RESET}"

else

    echo -e "${RED}[ERROR] Build failed.${RESET}"
    echo "Exit code: ${BUILD_STATUS}"

    echo
    echo "Compiler/linker diagnostics above were preserved."

fi

# ========================================
# 3. Tests
# ========================================

echo
echo "========================================"
echo "[3/3] RUNNING TESTS"
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

# ========================================
# Build artifacts
# ========================================

echo
echo "========================================"
echo "BUILD ARTIFACTS"
echo "========================================"
echo

if [[ -d "${BUILD_DIR}" ]]; then

    if [[ "${PRESET}" == *"win32"* ]]; then

        find "${BUILD_DIR}" \
            -type f \
            -name "*.exe" \
            -print \
            2>/dev/null

    else

        find "${BUILD_DIR}" \
            -maxdepth 3 \
            -type f \
            -perm -111 \
            -print \
            2>/dev/null

    fi

else

    echo "Build directory does not exist:"
    echo "  ${BUILD_DIR}"

fi

# ========================================
# Final result
# ========================================

echo
echo
echo "========================================"
echo " G U C H H O"
echo " FINAL RESULT"
echo "========================================"
echo

# ========================================
# Configure result
# ========================================

if [[ ${CONFIGURE_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[PASS] Configure${RESET}"

else

    echo -e "${RED}[FAIL] Configure${RESET}"

fi

# ========================================
# Build result
# ========================================

if [[ ${BUILD_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[PASS] Build${RESET}"

else

    echo -e "${RED}[FAIL] Build${RESET}"

fi

# ========================================
# Test result
# ========================================

if [[ ${TEST_STATUS} -eq 0 ]]; then

    echo -e "${GREEN}[PASS] Tests${RESET}"

else

    echo -e "${RED}[FAIL] Tests${RESET}"

fi

echo

# ========================================
# Failure count
# ========================================

FAILED_STAGES=0

if [[ ${CONFIGURE_STATUS} -ne 0 ]]; then
    FAILED_STAGES=$((FAILED_STAGES + 1))
fi

if [[ ${BUILD_STATUS} -ne 0 ]]; then
    FAILED_STAGES=$((FAILED_STAGES + 1))
fi

if [[ ${TEST_STATUS} -ne 0 ]]; then
    FAILED_STAGES=$((FAILED_STAGES + 1))
fi

# ========================================
# Success
# ========================================

if [[ ${FAILED_STAGES} -eq 0 ]]; then

    echo "========================================"
    echo -e "${GREEN} BUILD COMPLETED SUCCESSFULLY ${RESET}"
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
echo -e "${RED} BUILD COMPLETED WITH ERRORS ${RESET}"
echo "========================================"

echo
echo "Failed stages: ${FAILED_STAGES}"
echo

if [[ ${CONFIGURE_STATUS} -ne 0 ]]; then
    echo -e "${RED}[FAIL] CMake configuration${RESET}"
fi

if [[ ${BUILD_STATUS} -ne 0 ]]; then
    echo -e "${RED}[FAIL] Compilation / linking${RESET}"
fi

if [[ ${TEST_STATUS} -ne 0 ]]; then
    echo -e "${RED}[FAIL] Tests${RESET}"
fi

echo

if [[ -n "${LOG_FILE}" ]]; then

    echo "Complete output saved to:"
    echo "  ${LOG_FILE}"

else

    echo "For a complete diagnostic log run:"
    echo
    echo "  bash scripts/build.sh --all-errors --log build-errors.log"

fi

echo

exit 1