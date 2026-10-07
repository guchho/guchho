#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build-chocolatey.sh
# Build guchho Chocolatey package (.nupkg).
#
# Steps:
#   1. Build (or locate) the guchho.exe native binary
#   2. Copy it into package/chocolatey/guchho/tools/
#   3. Update version in guchho.nuspec and VERIFICATION.txt
#   4. Run `choco pack`
#
# Usage:
#   bash scripts/build-chocolatey.sh
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CHOCO_DIR="${ROOT_DIR}/package/chocolatey/guchho"
TOOLS_DIR="${CHOCO_DIR}/tools"
BINARY_NAME="guchho.exe"

# ========================================
# Helpers
# ========================================

# Node on Windows cannot resolve MSYS POSIX paths (/c/...).
# cygpath -m converts them to native paths (C:/Users/...).
to_winpath() {
    local p="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "${p}"
    else
        echo "${p}"
    fi
}

ROOT_DIR_WIN="$(to_winpath "${ROOT_DIR}")"
CHOCO_DIR_WIN="$(to_winpath "${CHOCO_DIR}")"

log() {
    echo "[Guchho] $*"
}

error() {
    echo
    echo "[ERROR] $*" >&2
    echo
    exit 1
}

# ========================================
# Check required tools
# ========================================

if ! command -v choco >/dev/null 2>&1; then
    error "Chocolatey was not found. Install it first: https://chocolatey.org/install"
fi

if ! command -v node >/dev/null 2>&1; then
    error "Node.js was not found. Install it first: https://nodejs.org"
fi

# ========================================
# Read version
# ========================================

VERSION=$(node -p "require('${ROOT_DIR_WIN}/package/npm/guchho/package.json').version")
echo "Building Chocolatey package: guchho@${VERSION}"
echo

# ========================================
# Build / locate the native binary
# ========================================

echo "========================================"
echo "[1/4] Preparing guchho.exe"
echo "========================================"
echo

PRESET="release-win32-x64-ninja"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"

SEARCH_PATHS=(
    "${BUILD_DIR}/bin/${BINARY_NAME}"
    "${BUILD_DIR}/bin/Release/${BINARY_NAME}"
    "${BUILD_DIR}/${BINARY_NAME}"
    "${BUILD_DIR}/Release/${BINARY_NAME}"
    "${ROOT_DIR}/build/release-win32-x64/bin/Release/${BINARY_NAME}"
    "${ROOT_DIR}/build/release-win32-x64/bin/${BINARY_NAME}"
)

BINARY_PATH=""

for candidate in "${SEARCH_PATHS[@]}"; do
    if [[ -f "${candidate}" ]]; then
        BINARY_PATH="${candidate}"
        break
    fi
done

if [[ -z "${BINARY_PATH}" ]]; then
    log "No existing binary found. Building '${PRESET}' preset..."
    cmake --preset "${PRESET}" --fresh
    cmake --build --preset "${PRESET}" --parallel

    for candidate in "${SEARCH_PATHS[@]}"; do
        if [[ -f "${candidate}" ]]; then
            BINARY_PATH="${candidate}"
            break
        fi
    done
fi

if [[ -z "${BINARY_PATH}" ]]; then
    error "Could not find the built guchho.exe. Build it first with: bash scripts/build.sh"
fi

log "Using binary:"
echo "  ${BINARY_PATH}"
echo

# ========================================
# Copy binary into package tools/
# ========================================

mkdir -p "${TOOLS_DIR}"
cp "${BINARY_PATH}" "${TOOLS_DIR}/${BINARY_NAME}"

if [[ ! -s "${TOOLS_DIR}/${BINARY_NAME}" ]]; then
    error "Copied binary is empty: ${TOOLS_DIR}/${BINARY_NAME}"
fi

log "Embedded: ${TOOLS_DIR}/${BINARY_NAME}"
echo

# ========================================
# Update version in nuspec
# ========================================

echo "========================================"
echo "[2/4] Updating package metadata"
echo "========================================"
echo

NUSPEC="${CHOCO_DIR}/guchho.nuspec"
NUSPEC_WIN="$(to_winpath "${NUSPEC}")"

if [[ ! -f "${NUSPEC}" ]]; then
    error "nuspec not found at ${NUSPEC}"
fi

node -e "
    const fs = require('fs');
    let content = fs.readFileSync(process.argv[1], 'utf8');
    content = content.replace(/<version>.*?<\/version>/, '<version>${VERSION}</version>');
    fs.writeFileSync(process.argv[1], content);
" "${NUSPEC_WIN}"

echo "  Updated nuspec version -> ${VERSION}"

# ========================================
# Update VERIFICATION.txt version
# ========================================
#
# VERIFICATION.txt is tracked in git as a template: the release URL names an
# artifact that does not exist until this build has run, so the file carries
# {VERSION} and the build owns the number. choco pack takes the tools/
# directory exactly as it finds it, so the placeholder is replaced in place
# for the duration of the pack and the original bytes are put back on the way
# out. Leaving the literal behind is not cosmetic: it is committed, and the
# next bump-version.sh verify fails on it -- which is how this file kept
# oscillating between a template and a stale download link.
#
# Two guards, because both failure modes have happened:
#   - a file already holding a literal makes the substitution below a silent
#     no-op, and the package would ship a URL pointing at the previous
#     release, so the placeholder is required before anything is written;
#   - a choco pack that errors exits the script before any restore would run,
#     so the restore is also a trap and not just a line after the pack.

VERIFY="${TOOLS_DIR}/VERIFICATION.txt"
VERIFY_WIN="$(to_winpath "${VERIFY}")"
VERIFY_TMPL=""

restore_verification() {
    if [[ -n "${VERIFY_TMPL}" && -f "${VERIFY_TMPL}" ]]; then
        cp "${VERIFY_TMPL}" "${VERIFY}"
        rm -f "${VERIFY_TMPL}"
    fi
}

if [[ -f "${VERIFY}" ]]; then
    if ! grep -q '{VERSION}' "${VERIFY}"; then
        error "VERIFICATION.txt holds a literal version instead of {VERSION} -- restore the placeholder (it is in git history) before building, or this package would ship a download URL for the wrong release."
    fi

    VERIFY_TMPL="$(mktemp)"
    cp "${VERIFY}" "${VERIFY_TMPL}"
    trap restore_verification EXIT

    node -e "
        const fs = require('fs');
        let content = fs.readFileSync(process.argv[1], 'utf8');
        content = content.replace(/v\{VERSION\}/g, 'v${VERSION}');
        content = content.replace(/\{VERSION\}/g, '${VERSION}');
        fs.writeFileSync(process.argv[1], content);
    " "${VERIFY_WIN}"
    echo "  Updated VERIFICATION.txt version -> ${VERSION} (template restored after pack)"
fi

echo

# ========================================
# Build .nupkg
# ========================================

echo "========================================"
echo "[3/4] Running choco pack"
echo "========================================"
echo

# choco pack writes the .nupkg next to the nuspec (which is gitignored).
cd "${CHOCO_DIR}"
choco pack "${NUSPEC_WIN}"

NUPKG="${CHOCO_DIR}/guchho.${VERSION}.nupkg"

if [[ ! -f "${NUPKG}" ]]; then
    error "Failed to create ${NUPKG}"
fi

FINAL_NUPKG="${NUPKG}"

# The pack succeeded, so the temporary literal has served its purpose. Put the
# template back now rather than at exit: a reader of the working tree should
# find the placeholder, and reading it back off disk is the only way to know
# the restore actually wrote what it meant to.
if [[ -f "${VERIFY}" ]]; then
    restore_verification

    if ! grep -q '{VERSION}' "${VERIFY}"; then
        error "${VERIFY} was not restored to its templated form -- check it back in as a template before committing."
    fi

    echo "  Restored VERIFICATION.txt template"
fi

echo
echo "========================================"
echo "[4/4] BUILD COMPLETE"
echo "========================================"
echo
echo "Package: ${FINAL_NUPKG}"
echo
echo "To test locally:"
echo "  choco install guchho --source '${CHOCO_DIR_WIN}'"
echo
echo "To publish:"
echo "  bash scripts/publish-chocolatey.sh"
echo