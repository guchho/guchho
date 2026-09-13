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

VERSION=$(node -p "require('${ROOT_DIR}/package/npm/guchho/package.json').version")
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

if [[ ! -f "${NUSPEC}" ]]; then
    error "nuspec not found at ${NUSPEC}"
fi

node -e "
    const fs = require('fs');
    let content = fs.readFileSync(process.argv[1], 'utf8');
    content = content.replace(/<version>.*?<\/version>/, '<version>${VERSION}</version>');
    fs.writeFileSync(process.argv[1], content);
" "${NUSPEC}"

echo "  Updated nuspec version -> ${VERSION}"

# ========================================
# Update VERIFICATION.txt version
# ========================================

VERIFY="${TOOLS_DIR}/VERIFICATION.txt"

if [[ -f "${VERIFY}" ]]; then
    node -e "
        const fs = require('fs');
        let content = fs.readFileSync(process.argv[1], 'utf8');
        content = content.replace(/v\{VERSION\}/g, 'v${VERSION}');
        content = content.replace(/\{VERSION\}/g, '${VERSION}');
        fs.writeFileSync(process.argv[1], content);
    " "${VERIFY}"
    echo "  Updated VERIFICATION.txt version -> ${VERSION}"
fi

echo

# ========================================
# Build .nupkg
# ========================================

echo "========================================"
echo "[3/4] Running choco pack"
echo "========================================"
echo

echo "========================================"
echo "[3/4] Running choco pack"
echo "========================================"
echo

# choco pack writes the .nupkg next to the nuspec (which is gitignored).
cd "${CHOCO_DIR}"
choco pack "${NUSPEC}"

NUPKG="${CHOCO_DIR}/guchho.${VERSION}.nupkg"

if [[ ! -f "${NUPKG}" ]]; then
    error "Failed to create ${NUPKG}"
fi

FINAL_NUPKG="${NUPKG}"

echo
echo "========================================"
echo "[4/4] BUILD COMPLETE"
echo "========================================"
echo
echo "Package: ${FINAL_NUPKG}"
echo
echo "To test locally:"
echo "  choco install guchho --source '${CHOCO_DIR}'"
echo
echo "To publish:"
echo "  bash scripts/publish-chocolatey.sh"
echo