#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build-npm.sh
#
# Build the Guchho native binary for the
# current platform and copy it into:
#
# package/npm/@guchho/<platform>/bin/
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
# Platform → binary name
# ========================================

platform_to_binary_name() {
    local platform="$1"

    case "${platform}" in
        win32-x64)
            echo "guchho.exe"
            ;;

        linux-x64|darwin-arm64)
            echo "guchho"
            ;;

        *)
            error "No binary name configured for: ${platform}"
            ;;
    esac
}


# ========================================
# Platform → npm package directory
# ========================================

platform_to_npm_dir() {
    local platform="$1"

    echo "${ROOT_DIR}/package/npm/@guchho/${platform}"
}


# ========================================
# Locate built binary
# ========================================

find_binary() {
    local preset="$1"
    local binary_name="$2"

    local build_dir="${ROOT_DIR}/build/${preset}"

    local search_paths=(
        "${build_dir}/bin/${binary_name}"
        "${build_dir}/bin/Release/${binary_name}"
        "${build_dir}/${binary_name}"
        "${build_dir}/Release/${binary_name}"
    )

    for candidate in "${search_paths[@]}"; do
        if [[ -f "${candidate}" ]]; then
            echo "${candidate}"
            return 0
        fi
    done

    return 1
}


# ========================================
# Main
# ========================================

main() {

    # ------------------------------------
    # Detect platform
    # ------------------------------------

    local platform
    platform="$(detect_platform)"

    local preset
    preset="$(platform_to_preset "${platform}")"

    local binary_name
    binary_name="$(platform_to_binary_name "${platform}")"

    local npm_dir
    npm_dir="$(platform_to_npm_dir "${platform}")"

    local bin_dir="${npm_dir}/bin"

    local build_dir="${ROOT_DIR}/build/${preset}"


    # ------------------------------------
    # Banner
    # ------------------------------------

    echo
    echo "========================================"
    echo " GUCHHO NPM BUILD"
    echo "========================================"
    echo
    echo "Platform : ${platform}"
    echo "Preset   : ${preset}"
    echo "Binary   : ${binary_name}"
    echo "npm dir  : ${npm_dir}"
    echo


    # ------------------------------------
    # Validate CMake preset
    # ------------------------------------

    log "Checking CMake preset..."

    if ! cmake --list-presets >/dev/null 2>&1; then
        error "CMake is not available."
    fi


    # ------------------------------------
    # Step 1: Build
    # ------------------------------------

    echo "========================================"
    echo "[1/3] Building binary"
    echo "========================================"
    echo

    log "Configuring: ${preset}"

    cmake --preset "${preset}" --fresh

    echo

    log "Building: ${preset}"

    cmake --build --preset "${preset}" --parallel


    # ------------------------------------
    # Step 2: Locate binary
    # ------------------------------------

    echo
    echo "========================================"
    echo "[2/3] Locating binary"
    echo "========================================"
    echo

    local binary_path

    if ! binary_path="$(find_binary "${preset}" "${binary_name}")"; then

        echo "Searched locations:"
        echo

        echo "  ${build_dir}/bin/${binary_name}"
        echo "  ${build_dir}/bin/Release/${binary_name}"
        echo "  ${build_dir}/${binary_name}"
        echo "  ${build_dir}/Release/${binary_name}"

        error "Could not find the built Guchho binary."
    fi

    log "Found:"
    echo "  ${binary_path}"


    # ------------------------------------
    # Step 3: Copy to npm package
    # ------------------------------------

    echo
    echo "========================================"
    echo "[3/3] Preparing npm package"
    echo "========================================"
    echo

    mkdir -p "${bin_dir}"

    local output_path="${bin_dir}/${binary_name}"

    cp "${binary_path}" "${output_path}"


    # ------------------------------------
    # Unix binary permissions
    # ------------------------------------

    if [[ "${platform}" != "win32-x64" ]]; then

        chmod +x "${output_path}"

        if command -v strip >/dev/null 2>&1; then
            log "Stripping debug symbols..."
            strip "${output_path}" 2>/dev/null || true
        fi

    fi


    # ------------------------------------
    # Validate output
    # ------------------------------------

    if [[ ! -f "${output_path}" ]]; then
        error "Failed to create npm binary: ${output_path}"
    fi

    if [[ ! -s "${output_path}" ]]; then
        error "Output binary is empty: ${output_path}"
    fi


    # ------------------------------------
    # Binary size
    # ------------------------------------

    local size

    if command -v du >/dev/null 2>&1; then
        size="$(du -h "${output_path}" | cut -f1)"
    else
        size="unknown"
    fi


    # ------------------------------------
    # Result
    # ------------------------------------

    echo
    echo "========================================"
    echo " BUILD COMPLETE"
    echo "========================================"
    echo
    echo "Platform : ${platform}"
    echo "Preset   : ${preset}"
    echo "Binary   : ${output_path}"
    echo "Size     : ${size}"
    echo
    echo "Next step:"
    echo "  scripts/publish-npm.sh"
    echo
}


main "$@"