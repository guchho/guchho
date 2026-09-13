#!/usr/bin/env bash

set -euo pipefail

# ========================================
# compile-commands.sh
#
# Generate a fresh compile_commands.json for
# the current platform and publish it to the
# project root so clangd and other tools pick
# it up by default.
#
# Steps:
#   1. Configure the platform CMake preset
#   2. Build it
#   3. Copy build/<preset>/compile_commands.json
#      to <root>/compile_commands.json
#
# Supported platforms:
#   win32-x64
#   linux-x64
#   darwin-arm64
# ========================================


# ========================================
# Project root
# ========================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"


# ========================================
# Helpers
# ========================================

log() {
    echo "[Guchho] $*"
}

error() {
    echo
    echo "[ERROR] $*" >&2
    echo
    exit 1
}

success() {
    echo
    echo "[OK] $*"
}


# ========================================
# Detect platform
# ========================================

detect_platform() {
    local os
    local arch

    case "$(uname -s)" in
        Linux*)
            os="linux"
            ;;

        Darwin*)
            os="darwin"
            ;;

        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            os="win32"
            ;;

        *)
            error "Unsupported operating system: $(uname -s)"
            ;;
    esac

    case "$(uname -m)" in
        x86_64|amd64)
            arch="x64"
            ;;

        aarch64|arm64)
            arch="arm64"
            ;;

        *)
            error "Unsupported architecture: $(uname -m)"
            ;;
    esac

    local platform="${os}-${arch}"

    case "${platform}" in
        win32-x64|linux-x64|darwin-arm64)
            echo "${platform}"
            ;;

        *)
            error "Guchho does not currently support platform: ${platform}"
            ;;
    esac
}


# ========================================
# Platform → CMake preset
#
# Only Ninja-based presets are used because
# they export compile_commands.json. The
# release variants carry the validated
# toolchain for each platform.
# ========================================

platform_to_preset() {
    local platform="$1"

    case "${platform}" in
        win32-x64)
            echo "release-win32-x64-ninja"
            ;;

        linux-x64)
            echo "release-linux-x64"
            ;;

        darwin-arm64)
            echo "release-darwin-arm64"
            ;;

        *)
            error "No CMake preset configured for: ${platform}"
            ;;
    esac
}


# ========================================
# Main
# ========================================

main() {

    # ------------------------------------
    # Detect platform and preset
    # ------------------------------------

    local platform
    platform="$(detect_platform)"

    local preset
    preset="$(platform_to_preset "${platform}")"

    local build_dir="${ROOT_DIR}/build/${preset}"
    local source_db="${build_dir}/compile_commands.json"
    local target_db="${ROOT_DIR}/compile_commands.json"


    # ------------------------------------
    # Banner
    # ------------------------------------

    echo
    echo "========================================"
    echo " GUCHHO COMPILE COMMANDS"
    echo "========================================"
    echo
    echo "Platform : ${platform}"
    echo "Preset   : ${preset}"
    echo "Source   : ${source_db}"
    echo "Target   : ${target_db}"
    echo


    # ------------------------------------
    # Validate CMake
    # ------------------------------------

    log "Checking CMake..."

    if ! cmake --list-presets >/dev/null 2>&1; then
        error "CMake is not available."
    fi


    # ------------------------------------
    # Step 1: Configure
    # ------------------------------------

    echo
    echo "========================================"
    echo "[1/3] Configuring"
    echo "========================================"
    echo

    log "Configuring: ${preset}"

    cmake --preset "${preset}"


    # ------------------------------------
    # Step 2: Build
    # ------------------------------------

    echo
    echo "========================================"
    echo "[2/3] Building"
    echo "========================================"
    echo

    log "Building: ${preset}"

    cmake --build --preset "${preset}" --parallel


    # ------------------------------------
    # Step 3: Publish compile_commands.json
    # ------------------------------------

    echo
    echo "========================================"
    echo "[3/3] Publishing compile_commands.json"
    echo "========================================"
    echo

    if [[ ! -f "${source_db}" ]]; then
        error "Preset did not produce a compile database: ${source_db}"
    fi

    cp "${source_db}" "${target_db}"

    if [[ ! -s "${target_db}" ]]; then
        error "Published compile database is empty: ${target_db}"
    fi


    # ------------------------------------
    # Result
    # ------------------------------------

    echo
    echo "========================================"
    echo " COMPILE COMMANDS READY"
    echo "========================================"
    echo
    echo "Wrote  : ${target_db}"
    echo "Preset : ${preset}"
    echo
    echo "clangd will pick this up from the"
    echo "repository root, and the VS Code"
    echo "extension reads the same database"
    echo "from build/${preset}/."
    echo
}


main "$@"