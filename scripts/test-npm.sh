#!/usr/bin/env bash

# ========================================
# Guchho npm package tests
# ========================================
#
# Runs the tests in package/npm/test/ — the manifest, the installer, the
# tarball, and the JavaScript API.
#
# These need no network and no npm install: the suite is plain node:test, so
# there is nothing to fetch before the tests can run. That is deliberate, and it
# is why this script is short.
#
# The C++ tests are not run from here. They need a build, they are slow, and
# scripts/test.sh already runs them. Pass --with-cpp to have this script drive
# that one as well, for a single command that covers both halves.
#
# Usage:
#   scripts/test-npm.sh [options]
#
# Options:
#   --with-cpp    Also run the C++ transform tests
#   --filter X    Only run JS test files whose name contains X
#   -h, --help    This message

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$REPO_ROOT/package/npm/test"

WITH_CPP=false
FILTER=""

# ========================================
# Colors
# ========================================

if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    RESET='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    CYAN=''
    RESET=''
fi

# ========================================
# Arguments
# ========================================

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-cpp) WITH_CPP=true; shift ;;
        --filter)   FILTER="${2:-}"; shift 2 ;;
        -h|--help)
            sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${RESET}"
            echo "Try 'scripts/test-npm.sh --help'"
            exit 2
            ;;
    esac
done

# ========================================
# Preconditions
# ========================================

command -v node >/dev/null 2>&1 || {
    echo -e "${RED}node is not on PATH.${RESET}"
    exit 1
}

if [[ ! -d "$TEST_DIR" ]]; then
    echo -e "${RED}No test directory at $TEST_DIR.${RESET}"
    exit 1
fi

# ========================================
# Run
# ========================================

NODE_VERSION="$(node --version)"
echo -e "${CYAN}node${RESET} $NODE_VERSION"

# The files are collected by bash and passed one by one rather than by handing
# node a directory or a glob. "node --test <dir>" stopped recursing at some
# point and "node --test <glob>" only appeared in later versions, and the
# package supports Node 18 — so a form that works on the newest Node would skip
# the tests on the version the publish workflow actually uses.
collect_tests() {
    local found=()

    for f in "$TEST_DIR"/*.test.js "$TEST_DIR"/*.test.mjs; do
        [[ -f "$f" ]] && found+=("$f")
    done

    printf '%s\n' "${found[@]:-}"
}

if [[ -n "$FILTER" ]]; then
    FILES=()
    while IFS= read -r f; do
        [[ -n "$f" ]] && FILES+=("$f")
    done < <(collect_tests | grep -F -- "$FILTER")

    if [[ ${#FILES[@]} -eq 0 ]]; then
        echo -e "${YELLOW}No test files matched '$FILTER'.${RESET}"
        exit 1
    fi

    echo -e "${CYAN}Running${RESET} ${#FILES[@]} file(s) matching '$FILTER'"
    node --test "${FILES[@]}"
else
    FILES=()
    while IFS= read -r f; do
        [[ -n "$f" ]] && FILES+=("$f")
    done < <(collect_tests)

    if [[ ${#FILES[@]} -eq 0 ]]; then
        echo -e "${RED}No test files found in $TEST_DIR.${RESET}"
        exit 1
    fi

    echo -e "${CYAN}Running${RESET} ${#FILES[@]} test file(s) in package/npm/test/"
    node --test "${FILES[@]}"
fi

JS_STATUS=$?

# ========================================
# Optional: the C++ tests
# ========================================

CPP_STATUS=0

if [[ "$WITH_CPP" == true ]]; then
    echo
    echo -e "${CYAN}Running${RESET} the C++ transform tests"

    if [[ ! -x "$SCRIPT_DIR/test.sh" ]]; then
        echo -e "${RED}scripts/test.sh is missing.${RESET}"
        CPP_STATUS=1
    else
        "$SCRIPT_DIR/test.sh" --filter CliTransform
        CPP_STATUS=$?
    fi
fi

# ========================================
# Result
# ========================================

echo

if [[ $JS_STATUS -eq 0 && $CPP_STATUS -eq 0 ]]; then
    echo -e "${GREEN}Passed.${RESET}"
    exit 0
fi

if [[ $JS_STATUS -ne 0 ]]; then
    echo -e "${RED}npm package tests failed.${RESET}"
fi
if [[ $CPP_STATUS -ne 0 ]]; then
    echo -e "${RED}C++ transform tests failed.${RESET}"
fi

exit 1
