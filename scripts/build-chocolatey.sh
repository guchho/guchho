#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build-chocolatey.sh
# Build guchho Chocolatey package (.nupkg).
#
# Steps:
#   1. Download guchho.exe from the official GitHub release archive
#   2. Copy it into package/chocolatey/tools/
#   3. Update version in guchho.nuspec and VERIFICATION.txt
#   4. Run `choco pack`
#
# Usage:
#   bash scripts/build-chocolatey.sh
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CHOCO_DIR="${ROOT_DIR}/package/chocolatey"
TOOLS_DIR="${CHOCO_DIR}/tools"
BINARY_NAME="guchho.exe"

# Temporary download directory; removed by the EXIT trap.
DOWNLOAD_DIR=""

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

# The single EXIT trap: bash replaces rather than chains traps, so both
# undo-actions live here. restore_verification is defined further down (it
# needs VERIFY_TMPL, which starts empty and makes this a no-op until the
# template has been substituted), and the download directory is removed
# whenever it was created.
cleanup() {
    if declare -F restore_verification >/dev/null 2>&1; then
        restore_verification
    fi

    if [[ -n "${DOWNLOAD_DIR}" && -d "${DOWNLOAD_DIR}" ]]; then
        rm -rf "${DOWNLOAD_DIR}"
    fi
}

trap cleanup EXIT

# ========================================
# Check required tools
# ========================================

if ! command -v choco >/dev/null 2>&1; then
    error "Chocolatey was not found. Install it first: https://chocolatey.org/install"
fi

if ! command -v node >/dev/null 2>&1; then
    error "Node.js was not found. Install it first: https://nodejs.org"
fi

if ! command -v curl >/dev/null 2>&1; then
    error "curl was not found. It downloads the release archive: https://curl.se"
fi

if ! command -v unzip >/dev/null 2>&1; then
    error "unzip was not found. It extracts guchho.exe from the release archive."
fi

# ========================================
# Read version
# ========================================

VERSION=$(node -p "require('${ROOT_DIR_WIN}/package/npm/guchho/package.json').version")
echo "Building Chocolatey package: guchho@${VERSION}"
echo

# ========================================
# Download the native binary from the
# official GitHub release
# ========================================

echo "========================================"
echo "[1/4] Downloading guchho.exe"
echo "========================================"
echo

RELEASE_TAG="v${VERSION}"
ARCHIVE_NAME="guchho-${VERSION}-win32-x64.zip"
RELEASE_URL="https://github.com/guchho/guchho/releases/download/${RELEASE_TAG}/${ARCHIVE_NAME}"

DOWNLOAD_DIR="$(mktemp -d)"
EXTRACT_DIR="${DOWNLOAD_DIR}/extracted"
mkdir -p "${EXTRACT_DIR}"

log "Downloading:"
echo "  ${RELEASE_URL}"
echo

if ! curl -fsSL --retry 3 -o "${DOWNLOAD_DIR}/${ARCHIVE_NAME}" "${RELEASE_URL}"; then
    error "Could not download ${RELEASE_URL}
        The ${RELEASE_TAG} release must exist and carry the ${ARCHIVE_NAME}
        asset before the Chocolatey package can be built. Publish it first:
        the npm-publish.yml win32-x64 run uploads guchho-win32-x64.zip (see
        CONTRIBUTING.md, 'Downloadable Windows x64 Release Archive')."
fi

# CPack installs the binary under bin/, inside a versioned top-level folder
# (guchho-<version>-win32-x64/bin/guchho.exe), alongside headers and docs the
# package does not embed. The wildcard tolerates the folder name without
# hardcoding the version twice.
if ! unzip -o -q "${DOWNLOAD_DIR}/${ARCHIVE_NAME}" '*/bin/guchho.exe' -d "${EXTRACT_DIR}"; then
    error "${ARCHIVE_NAME} does not contain */bin/guchho.exe -- the release archive layout changed; update this script."
fi

BINARY_PATH=""

for candidate in "${EXTRACT_DIR}"/*/bin/"${BINARY_NAME}" "${EXTRACT_DIR}/bin/${BINARY_NAME}"; do
    if [[ -f "${candidate}" ]]; then
        BINARY_PATH="${candidate}"
        break
    fi
done

if [[ -z "${BINARY_PATH}" ]]; then
    error "guchho.exe was not found inside ${ARCHIVE_NAME} after extraction."
fi

if [[ ! -s "${BINARY_PATH}" ]]; then
    error "Extracted binary is empty: ${BINARY_PATH}"
fi

log "Extracted:"
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

# VERIFICATION.txt step 2 asks users to compare this hash against the release
# binary; printing it here is what makes that comparison a copy-paste.
if command -v sha256sum >/dev/null 2>&1; then
    echo "  SHA256: $(sha256sum "${TOOLS_DIR}/${BINARY_NAME}" | cut -d' ' -f1)"
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
#     so the restore runs from the shared EXIT trap (cleanup, near the top of
#     this script) and not just as a line after the pack.

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